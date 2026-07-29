// ============================================================================
// Sentinel AntiCheat - Behavior Detector Implementation
// ============================================================================
// Analyzes behavioral patterns for signs of cheating including impossible
// consistency, pattern repetition, and statistical anomalies.
// ============================================================================

#include "BehaviorDetector.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace sentinel::modules::behavior {

BehaviorDetector::BehaviorDetector(IPlayerManager* playerMgr, ILogger* logger)
    : m_playerManager(playerMgr)
    , m_logger(logger)
{
    m_config.name = m_name;
    m_config.description = m_desc;
    m_config.enabled = true;
    m_config.riskScore = 20;
    m_config.threshold = 0.5f;
    m_config.minSamples = 5;
    m_config.cooldownMs = 10000;
}

const std::string& BehaviorDetector::name() const noexcept { return m_name; }
const std::string& BehaviorDetector::description() const noexcept { return m_desc; }
DetectionType BehaviorDetector::detectionType() const noexcept {
    return DetectionType::PerfectTracking; // Using existing type, would need a new behavior type
}

DetectionResult BehaviorDetector::detect(const std::string& playerKey, float deltaTime) {
    std::lock_guard lock(m_mutex);
    auto& data = getOrCreateData(playerKey);

    DetectionResult result;
    result.moduleName = m_name;
    result.type = detectionType();

    if (!m_config.enabled || data.sampleCount < m_config.minSamples) {
        return result;
    }

    DetectionResult candidates[] = {
        detectConsistency(data),
        detectRepeatedPatterns(data),
        detectStreakAnomaly(data),
        detectHeadshotRatio(data),
        detectCrossRoundAnomalies(data)
    };

    float maxConfidence = 0.0f;
    for (const auto& candidate : candidates) {
        if (candidate.confidence > maxConfidence) {
            maxConfidence = candidate.confidence;
            result = candidate;
        }
    }

    return result;
}

void BehaviorDetector::reset(const std::string& playerKey) {
    std::lock_guard lock(m_mutex);
    m_playerData.erase(playerKey);
}

void BehaviorDetector::resetAll() {
    std::lock_guard lock(m_mutex);
    m_playerData.clear();
}

const DetectorConfig& BehaviorDetector::config() const noexcept { return m_config; }
void BehaviorDetector::configure(const DetectorConfig& cfg) { m_config = cfg; }
bool BehaviorDetector::requiresFrameUpdate() const noexcept { return false; }
void BehaviorDetector::onFrameUpdate(float deltaTime) {}

float BehaviorDetector::currentSuspicion(const std::string& playerKey) const noexcept {
    std::lock_guard lock(m_mutex);
    auto it = m_playerData.find(playerKey);
    if (it == m_playerData.end()) return 0.0f;
    return it->second.consistencyScore;
}

void BehaviorDetector::recordKill(const std::string& playerKey, const std::string& weapon,
                                    bool headshot, float distance) {
    std::lock_guard lock(m_mutex);
    auto& data = getOrCreateData(playerKey);

    data.totalKills++;
    data.killDistances.push_back(distance);
    data.killTimestamps.push_back(std::chrono::steady_clock::now());

    // Track weapon usage
    data.weaponKills[weapon]++;

    // Calculate kill intervals
    if (data.killTimestamps.size() >= 2) {
        auto interval = std::chrono::duration_cast<std::chrono::milliseconds>(
            data.killTimestamps.back() - data.killTimestamps[data.killTimestamps.size() - 2]
        ).count();
        data.killIntervals.push_back(static_cast<float>(interval) / 1000.0f);
    }

    // Update averages
    if (!data.killDistances.empty()) {
        float sum = std::accumulate(data.killDistances.begin(), data.killDistances.end(), 0.0f);
        data.avgKillDistance = sum / static_cast<float>(data.killDistances.size());
    }

    if (!data.killIntervals.empty()) {
        float sum = std::accumulate(data.killIntervals.begin(), data.killIntervals.end(), 0.0f);
        data.avgKillInterval = sum / static_cast<float>(data.killIntervals.size());
    }

    // Trim
    if (data.killDistances.size() > BehaviorData::MAX_HISTORY) {
        data.killDistances.pop_front();
    }
    if (data.killIntervals.size() > BehaviorData::MAX_HISTORY) {
        data.killIntervals.pop_front();
    }

    data.sampleCount = static_cast<uint32_t>(data.totalKills);
}

void BehaviorDetector::recordDeath(const std::string& playerKey) {
    std::lock_guard lock(m_mutex);
    auto& data = getOrCreateData(playerKey);
    data.totalDeaths++;
    data.killStreaks.push_back(0); // Kill streak reset
}

void BehaviorDetector::recordRoundEnd(const std::string& playerKey, uint32_t killsInRound,
                                        uint32_t deathsInRound, uint32_t headshotsInRound) {
    std::lock_guard lock(m_mutex);
    auto& data = getOrCreateData(playerKey);

    data.killsPerRound.push_back(killsInRound);
    data.headshotsPerRound.push_back(headshotsInRound);

    // Calculate per-round scores
    float roundScore = static_cast<float>(killsInRound) /
                       std::max(1.0f, static_cast<float>(deathsInRound));
    data.roundScores.push_back(roundScore);

    // Update headshot ratio
    uint32_t totalKills = std::accumulate(data.killsPerRound.begin(), data.killsPerRound.end(), 0u);
    uint32_t totalHeadshots = std::accumulate(data.headshotsPerRound.begin(), data.headshotsPerRound.end(), 0u);
    data.headshotRatio = totalKills > 0
        ? static_cast<float>(totalHeadshots) / static_cast<float>(totalKills)
        : 0.0f;

    // Calculate consistency metrics
    if (data.roundScores.size() >= 2) {
        float sum = std::accumulate(data.roundScores.begin(), data.roundScores.end(), 0.0f);
        data.scoreMean = sum / static_cast<float>(data.roundScores.size());
        float sqSum = 0.0f;
        for (float s : data.roundScores) {
            sqSum += (s - data.scoreMean) * (s - data.scoreMean);
        }
        data.scoreStdDev = std::sqrt(sqSum / static_cast<float>(data.roundScores.size()));
        data.consistencyRatio = data.scoreStdDev > 0
            ? data.scoreMean / data.scoreStdDev : 0.0f;
    }

    // Trim
    if (data.killsPerRound.size() > BehaviorData::MAX_HISTORY) {
        data.killsPerRound.pop_front();
        data.headshotsPerRound.pop_front();
        data.roundScores.pop_front();
    }
}

void BehaviorDetector::recordAimPattern(const std::string& playerKey, uint32_t patternHash) {
    std::lock_guard lock(m_mutex);
    auto& data = getOrCreateData(playerKey);
    data.aimPatternHashes.push_back(patternHash);
    data.totalPatterns++;

    // Check for repeated patterns
    if (data.aimPatternHashes.size() >= 2) {
        auto last = data.aimPatternHashes.back();
        auto secondLast = data.aimPatternHashes[data.aimPatternHashes.size() - 2];
        if (last == secondLast) {
            data.repeatedPatterns++;
        }
    }

    if (data.aimPatternHashes.size() > BehaviorData::MAX_HISTORY) {
        data.aimPatternHashes.pop_front();
    }
}

// ── Detection Subroutines ────────────────────────────────────────────────

DetectionResult BehaviorDetector::detectConsistency(BehaviorData& data) {
    DetectionResult result;
    result.moduleName = m_name;

    if (data.roundScores.size() < 3) return result;

    // Extremely high consistency ratio indicates robotic play
    if (data.consistencyRatio > BehaviorData::HIGH_CONSISTENCY_RATIO) {
        result.detected = true;
        result.confidence = std::min(1.0f, data.consistencyRatio / 10.0f);
        result.score = static_cast<uint32_t>(m_consistencyWeight * result.confidence);
        result.evidence = "Impossible consistency: ratio " +
            std::to_string(static_cast<int>(data.consistencyRatio)) +
            " across " + std::to_string(data.roundScores.size()) + " rounds";
    }

    return result;
}

DetectionResult BehaviorDetector::detectRepeatedPatterns(BehaviorData& data) {
    DetectionResult result;
    result.moduleName = m_name;

    if (data.totalPatterns < 10) return result;

    float repeatRatio = static_cast<float>(data.repeatedPatterns) /
                        static_cast<float>(data.totalPatterns);

    if (repeatRatio > 0.5f) {
        result.detected = true;
        result.confidence = repeatRatio;
        result.score = static_cast<uint32_t>(m_patternWeight * result.confidence);
        result.evidence = "Repeated aim patterns: " +
            std::to_string(data.repeatedPatterns) + "/" +
            std::to_string(data.totalPatterns) + " identical";
    }

    return result;
}

DetectionResult BehaviorDetector::detectStreakAnomaly(BehaviorData& data) {
    DetectionResult result;
    result.moduleName = m_name;

    if (data.totalKills < 10) return result;

    data.maxKillStreak = data.killStreaks.empty() ? 0 :
        *std::max_element(data.killStreaks.begin(), data.killStreaks.end());

    // Multiple very long kill streaks are suspicious
    uint32_t longStreaks = 0;
    for (uint32_t s : data.killStreaks) {
        if (s >= 5) longStreaks++;
    }

    float streakRatio = data.killStreaks.empty() ? 0 :
        static_cast<float>(longStreaks) / static_cast<float>(data.killStreaks.size());

    if (streakRatio > 0.3f && data.maxKillStreak > 10) {
        result.detected = true;
        result.confidence = std::min(1.0f, streakRatio * 2.0f);
        result.score = static_cast<uint32_t>(m_streakWeight * result.confidence);
        result.evidence = "Kill streak anomaly: " + std::to_string(data.maxKillStreak) +
            " max streak, " + std::to_string(longStreaks) + " streaks >= 5 kills";
    }

    return result;
}

DetectionResult BehaviorDetector::detectHeadshotRatio(BehaviorData& data) {
    DetectionResult result;
    result.moduleName = m_name;

    if (data.totalKills < 10) return result;

    // CS pros average ~30-40% headshot ratio with rifles
    // > 70% sustained across many kills is highly suspicious
    if (data.headshotRatio > BehaviorData::SUSPICIOUS_HS_RATIO && data.totalKills > 30) {
        result.detected = true;
        result.confidence = std::min(1.0f, (data.headshotRatio - 0.7f) * 3.0f);
        result.score = static_cast<uint32_t>(m_headshotWeight * result.confidence);
        result.evidence = "Impossible headshot ratio: " +
            std::to_string(static_cast<int>(data.headshotRatio * 100)) +
            "% (" + std::to_string(data.totalKills) + " kills)";
    }

    return result;
}

DetectionResult BehaviorDetector::detectCrossRoundAnomalies(BehaviorData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    return result;
}

BehaviorData& BehaviorDetector::getOrCreateData(const std::string& playerKey) {
    auto it = m_playerData.find(playerKey);
    if (it != m_playerData.end()) return it->second;
    return m_playerData[playerKey];
}

} // namespace sentinel::modules::behavior
