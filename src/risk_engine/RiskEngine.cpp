// ============================================================================
// Sentinel AntiCheat - Risk Engine Implementation
// ============================================================================
// Core scoring engine that accumulates detection evidence, applies decay,
// and determines enforcement actions.
// ============================================================================

#include "RiskEngine.h"
#include <algorithm>
#include <numeric>

namespace sentinel::risk {

RiskEngine::RiskEngine(ILogger* logger)
    : m_logger(logger)
{
    // Default thresholds
    m_thresholds.safeLevel = 39;
    m_thresholds.suspiciousLevel = 69;
    m_thresholds.highlySuspiciousLevel = 99;
    m_thresholds.cheatingLevel = 149;

    // Default action config
    m_config.logThreshold = 15;
    m_config.notifyAdminThreshold = 40;
    m_config.recordDemoThreshold = 70;
    m_config.kickThreshold = 100;
    m_config.tempBanThreshold = 120;
    m_config.permBanThreshold = 200;
    m_config.requiredDetections = 3;
    m_config.timeWindowSeconds = 300;
    m_config.autoKick = true;
    m_config.autoTempBan = true;
    m_config.autoPermBan = false;
    m_config.tempBanDurationMins = 60;
}

RiskScore RiskEngine::processDetection(
    const std::string& playerKey,
    const DetectionEvent& event)
{
    std::lock_guard lock(m_mutex);
    auto& state = getOrCreateState(playerKey);

    // Trim old events outside the time window
    auto cutoff = std::chrono::system_clock::now() -
        std::chrono::seconds(m_config.timeWindowSeconds);

    while (!state.recentEvents.empty() && state.recentEvents.front().timestamp < cutoff) {
        state.recentEvents.pop_front();
    }

    // Add new event
    // Apply time-based decay before adding new score
    applyDecay(state, 0.0f);  // Force decay check with explicit elapsed calculation

    state.recentEvents.push_back(event);
    state.totalDetections++;

    // Update score by category
    auto typeVal = static_cast<uint16_t>(event.type);
    if (typeVal <= 0x00FF) {
        state.current.aimScore += event.score;
    } else if (typeVal <= 0x0FFF) {
        state.current.movementScore += event.score;
    } else {
        state.current.networkScore += event.score;
    }

    state.current.detectionCount = static_cast<uint32_t>(state.recentEvents.size());
    state.current.update();
    state.lastActivity = std::chrono::steady_clock::now();

    // Store in history
    state.history.push_back(state.current);
    if (state.history.size() > MAX_HISTORY) {
        state.history.pop_front();
    }

    // Log the detection
    if (m_logger) {
        m_logger->logDetection(event);
    }

    // Notify callbacks
    for (auto& cb : m_detectionCallbacks) {
        cb(event);
    }
    for (auto& cb : m_riskCallbacks) {
        cb(playerKey, state.current);
    }

    // Check if escalation is needed
    checkEscalation(playerKey);

    return state.current;
}

RiskScore RiskEngine::getPlayerRisk(const std::string& playerKey) const {
    std::lock_guard lock(m_mutex);
    auto it = m_players.find(playerKey);
    if (it != m_players.end()) {
        return it->second.current;
    }
    return RiskScore{};
}

std::vector<RiskScore> RiskEngine::getRiskHistory(
    const std::string& playerKey, size_t count) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_players.find(playerKey);
    if (it == m_players.end()) return {};

    std::vector<RiskScore> result;
    size_t start = (it->second.history.size() > count)
        ? it->second.history.size() - count : 0;

    for (size_t i = start; i < it->second.history.size(); ++i) {
        result.push_back(it->second.history[i]);
    }
    return result;
}

ActionType RiskEngine::determineAction(const RiskScore& score) const noexcept {
    if (score.totalScore >= m_config.permBanThreshold && m_config.autoPermBan) {
        return ActionType::PermBan;
    }
    if (score.totalScore >= m_config.tempBanThreshold && m_config.autoTempBan) {
        return ActionType::TempBan;
    }
    if (score.totalScore >= m_config.kickThreshold && m_config.autoKick) {
        return ActionType::Kick;
    }
    if (score.totalScore >= m_config.recordDemoThreshold) {
        return ActionType::RecordDemo;
    }
    if (score.totalScore >= m_config.notifyAdminThreshold) {
        return ActionType::NotifyAdmin;
    }
    if (score.totalScore >= m_config.logThreshold) {
        return ActionType::Log;
    }
    return ActionType::None;
}

void RiskEngine::executeAction(
    const std::string& playerKey,
    ActionType action,
    const std::string& reason)
{
    std::lock_guard lock(m_mutex);

    BanRecord ban;
    ban.playerKey = playerKey;
    ban.reason = reason;
    ban.banType = action;
    ban.issuedAt = std::chrono::system_clock::now();
    ban.issuedBy = "system";

    switch (action) {
        case ActionType::None:
            break;

        case ActionType::Log:
            m_logger->warn("RiskEngine", "Action: Log - " + reason, playerKey);
            break;

        case ActionType::NotifyAdmin:
            m_logger->log(LogLevel::Admin, "RiskEngine",
                "NOTIFY: " + reason, playerKey);
            break;

        case ActionType::RecordDemo:
            m_logger->info("RiskEngine", "Recording demo for " + playerKey);
            break;

        case ActionType::Kick:
            m_logger->log(LogLevel::Admin, "RiskEngine",
                "KICK: " + reason, playerKey);
            if (m_logger) {
                m_logger->logBan(ban);
            }
            break;

        case ActionType::TempBan:
            ban.expiresAt = ban.issuedAt +
                std::chrono::minutes(m_config.tempBanDurationMins);
            ban.active = true;
            m_logger->log(LogLevel::Ban, "RiskEngine",
                "TEMP BAN (" + std::to_string(m_config.tempBanDurationMins) + "min): " + reason,
                playerKey);
            if (m_logger) {
                m_logger->logBan(ban);
            }
            for (auto& cb : m_banCallbacks) cb(ban);
            break;

        case ActionType::PermBan:
            ban.expiresAt = ban.issuedAt + std::chrono::hours(99999);
            ban.active = true;
            m_logger->log(LogLevel::Ban, "RiskEngine",
                "PERM BAN: " + reason, playerKey);
            if (m_logger) {
                m_logger->logBan(ban);
            }
            for (auto& cb : m_banCallbacks) cb(ban);
            break;
    }
}

void RiskEngine::resetPlayer(const std::string& playerKey) {
    std::lock_guard lock(m_mutex);
    m_players.erase(playerKey);
}

std::vector<std::pair<std::string, RiskScore>> RiskEngine::getAllPlayerRisks() const {
    std::lock_guard lock(m_mutex);
    std::vector<std::pair<std::string, RiskScore>> result;
    for (const auto& [key, state] : m_players) {
        result.emplace_back(key, state.current);
    }
    // Sort by total score descending
    std::sort(result.begin(), result.end(),
        [](const auto& a, const auto& b) {
            return a.second.totalScore > b.second.totalScore;
        });
    return result;
}

void RiskEngine::onRiskChanged(RiskCallback callback) {
    m_riskCallbacks.push_back(std::move(callback));
}

void RiskEngine::onDetection(DetectionCallback callback) {
    m_detectionCallbacks.push_back(std::move(callback));
}

void RiskEngine::onBan(BanCallback callback) {
    m_banCallbacks.push_back(std::move(callback));
}

const RiskActionConfig& RiskEngine::actionConfig() const noexcept {
    return m_config;
}

void RiskEngine::configure(const RiskActionConfig& cfg) {
    std::lock_guard lock(m_mutex);
    m_config = cfg;
}

void RiskEngine::updateDecay(float deltaTime) {
    std::lock_guard lock(m_mutex);
    for (auto& [key, state] : m_players) {
        applyDecay(state, deltaTime);
    }
}

void RiskEngine::checkEscalation(const std::string& playerKey) {
    auto it = m_players.find(playerKey);
    if (it == m_players.end()) return;

    auto& state = it->second;
    if (state.totalDetections < m_config.requiredDetections) return;

    auto action = determineAction(state.current);
    if (action != ActionType::None) {
        std::string reason = "Risk score " + std::to_string(state.current.totalScore) +
            " exceeded threshold. Detections: " + std::to_string(state.totalDetections);
        executeAction(playerKey, action, reason);
    }
}

void RiskEngine::applyDecay(PlayerRiskState& state, float deltaTime) {
    // Only decay if player hasn't had recent activity
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - state.lastActivity).count();

    if (deltaTime > 0.0f) {
        // Called from main update loop with deltaTime
    }

    if (elapsed < 30) return;  // Don't decay within 30 seconds of activity

    // Apply exponential decay to each category
    float decayFactor = DECAY_RATE * deltaTime;
    state.current.aimScore = static_cast<uint32_t>(
        std::max(0.0f, static_cast<float>(state.current.aimScore) - decayFactor * 2.0f)
    );
    state.current.movementScore = static_cast<uint32_t>(
        std::max(0.0f, static_cast<float>(state.current.movementScore) - decayFactor)
    );
    state.current.networkScore = static_cast<uint32_t>(
        std::max(0.0f, static_cast<float>(state.current.networkScore) - decayFactor)
    );
    state.current.behaviorScore = static_cast<uint32_t>(
        std::max(0.0f, static_cast<float>(state.current.behaviorScore) - decayFactor * 0.5f)
    );

    state.current.update();

    // Remove old events beyond decay threshold
    float totalDecay = DECAY_RATE * static_cast<float>(elapsed);
    if (totalDecay > MAX_DECAY) {
        while (state.recentEvents.size() > 10) {
            state.recentEvents.pop_front();
        }
    }
}

uint32_t RiskEngine::calculateWeightedScore(const PlayerRiskState& state) const {
    if (state.recentEvents.empty()) return 0;

    // More recent events get higher weight
    float weightedScore = 0.0f;
    size_t count = state.recentEvents.size();
    size_t i = 0;

    for (const auto& event : state.recentEvents) {
        float weight = static_cast<float>(i + 1) / static_cast<float>(count);
        weightedScore += event.score * weight;
        i++;
    }

    return static_cast<uint32_t>(weightedScore / count);
}

PlayerRiskState& RiskEngine::getOrCreateState(const std::string& playerKey) {
    auto it = m_players.find(playerKey);
    if (it != m_players.end()) return it->second;

    auto& state = m_players[playerKey];
    state.lastActivity = std::chrono::steady_clock::now();
    return state;
}

} // namespace sentinel::risk
