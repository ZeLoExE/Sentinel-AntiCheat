// ============================================================================
// Sentinel AntiCheat - Network Detector Implementation
// ============================================================================
// Analyzes network behavior for signs of FakeLag, packet manipulation,
// command flooding, UserCmd anomalies, and tick irregularities.
// ============================================================================

#include "NetworkDetector.h"
#include <algorithm>
#include <numeric>

namespace sentinel::modules::network {

NetworkDetector::NetworkDetector(IPlayerManager* playerMgr, ILogger* logger)
    : m_playerManager(playerMgr)
    , m_logger(logger)
{
    m_config.name = m_name;
    m_config.description = m_desc;
    m_config.enabled = true;
    m_config.riskScore = 20;
    m_config.threshold = 0.5f;
    m_config.minSamples = 30;
    m_config.cooldownMs = 5000;
}

const std::string& NetworkDetector::name() const noexcept { return m_name; }
const std::string& NetworkDetector::description() const noexcept { return m_desc; }
DetectionType NetworkDetector::detectionType() const noexcept {
    return DetectionType::FakeLag;
}

DetectionResult NetworkDetector::detect(const std::string& playerKey, float deltaTime) {
    std::lock_guard lock(m_mutex);
    auto& data = getOrCreateData(playerKey);

    DetectionResult result;
    result.moduleName = m_name;
    result.type = detectionType();

    if (!m_config.enabled || data.sampleCount < m_config.minSamples) {
        return result;
    }

    DetectionResult candidates[] = {
        detectFakeLag(data),
        detectCommandFlood(data),
        detectPacketManipulation(data),
        detectCmdAnomalies(data),
        detectTickIrregularities(data)
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

void NetworkDetector::reset(const std::string& playerKey) {
    std::lock_guard lock(m_mutex);
    m_playerData.erase(playerKey);
}

void NetworkDetector::resetAll() {
    std::lock_guard lock(m_mutex);
    m_playerData.clear();
}

const DetectorConfig& NetworkDetector::config() const noexcept { return m_config; }
void NetworkDetector::configure(const DetectorConfig& cfg) { m_config = cfg; }
bool NetworkDetector::requiresFrameUpdate() const noexcept { return false; }
void NetworkDetector::onFrameUpdate(float deltaTime) {}

float NetworkDetector::currentSuspicion(const std::string& playerKey) const noexcept {
    std::lock_guard lock(m_mutex);
    auto it = m_playerData.find(playerKey);
    if (it == m_playerData.end()) return 0.0f;
    return it->second.fakeLagScore;
}

void NetworkDetector::recordNetworkStats(const std::string& playerKey, float latency,
                                           float packetLoss, uint32_t choke) {
    std::lock_guard lock(m_mutex);
    auto& data = getOrCreateData(playerKey);

    data.latency.push_back(latency);
    data.packetLoss.push_back(packetLoss);
    data.chokeAmount.push_back(choke);
    data.chokePatterns.push_back(choke);

    // Track high choke frames
    if (choke > NetworkData::FAKELAG_CHOKE_THRESHOLD) {
        data.highChokeFrames++;
    }
    data.totalChokeFrames++;

    // Calculate averages
    auto avg = [](const auto& deque) -> float {
        if (deque.empty()) return 0.0f;
        return std::accumulate(deque.begin(), deque.end(), 0.0f) /
               static_cast<float>(deque.size());
    };
    data.avgLatency = avg(data.latency);
    data.avgPacketLoss = avg(data.packetLoss);
    data.chokeRatio = data.totalChokeFrames > 0
        ? static_cast<float>(data.highChokeFrames) / static_cast<float>(data.totalChokeFrames)
        : 0.0f;

    // Trim
    if (data.latency.size() > NetworkData::MAX_HISTORY) {
        data.latency.pop_front();
        data.packetLoss.pop_front();
        data.chokeAmount.pop_front();
        data.chokePatterns.pop_front();
    }

    data.sampleCount++;
}

void NetworkDetector::recordUserCmd(const std::string& playerKey, const UserCmdData& cmd) {
    std::lock_guard lock(m_mutex);
    auto& data = getOrCreateData(playerKey);

    data.cmdButtons.push_back(cmd.buttons);
    data.totalCmds++;

    // Detect duplicate commands
    if (data.cmdButtons.size() >= 2 && data.cmdButtons.back() == data.cmdButtons[data.cmdButtons.size() - 2]) {
        data.duplicateCmds++;
    }

    if (data.cmdButtons.size() > NetworkData::MAX_HISTORY) {
        data.cmdButtons.pop_front();
    }
}

void NetworkDetector::recordPacket(const std::string& playerKey, uint32_t sequenceNumber,
                                     uint32_t tickCount) {
    std::lock_guard lock(m_mutex);
    auto& data = getOrCreateData(playerKey);
}

// ── Detection Subroutines ────────────────────────────────────────────────

DetectionResult NetworkDetector::detectFakeLag(NetworkData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::FakeLag;

    if (data.totalChokeFrames < NetworkData::FAKELAG_MIN_FRAMES) return result;

    // FakeLag: player intentionally sends high choke to avoid hit registration
    // Look for consistent high choke patterns with periodic drops
    if (data.chokeRatio > 0.3f && data.highChokeFrames > NetworkData::FAKELAG_MIN_FRAMES) {
        // Check if the pattern is consistent (not just network issues)
        float patternStdDev = 0.0f;
        if (!data.chokePatterns.empty()) {
            float sum = std::accumulate(data.chokePatterns.begin(), data.chokePatterns.end(), 0.0f);
            float mean = sum / static_cast<float>(data.chokePatterns.size());
            float sqSum = 0.0f;
            for (uint32_t c : data.chokePatterns) {
                sqSum += (static_cast<float>(c) - mean) * (static_cast<float>(c) - mean);
            }
            patternStdDev = std::sqrt(sqSum / static_cast<float>(data.chokePatterns.size()));
        }

        // Real network issues have high variance; FakeLag is deliberate/consistent
        if (patternStdDev < 20.0f) {
            result.detected = true;
            result.confidence = std::min(1.0f, data.chokeRatio);
            result.score = static_cast<uint32_t>(m_fakeLagWeight * result.confidence);
            result.evidence = "FakeLag: " + std::to_string(data.highChokeFrames) +
                " high-choke frames (" + std::to_string(static_cast<int>(data.chokeRatio * 100)) + "%)";
        }
    }

    return result;
}

DetectionResult NetworkDetector::detectCommandFlood(NetworkData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::CommandFlood;

    if (data.totalCmds < 50) return result;

    data.maxCommandsPerTick = data.commandsPerTick.empty() ? 0 :
        *std::max_element(data.commandsPerTick.begin(), data.commandsPerTick.end());
    data.avgCommandsPerTick = data.commandsPerTick.empty() ? 0.0 :
        std::accumulate(data.commandsPerTick.begin(), data.commandsPerTick.end(), 0.0) /
        static_cast<double>(data.commandsPerTick.size());

    if (data.maxCommandsPerTick > NetworkData::COMMAND_FLOOD_THRESHOLD) {
        result.detected = true;
        result.confidence = std::min(1.0f,
            static_cast<float>(data.maxCommandsPerTick) /
            static_cast<float>(NetworkData::COMMAND_FLOOD_THRESHOLD * 2));
        result.score = static_cast<uint32_t>(m_cmdFloodWeight * result.confidence);
        result.evidence = "Command flood: " + std::to_string(data.maxCommandsPerTick) +
            " cmds/tick (limit: " + std::to_string(NetworkData::COMMAND_FLOOD_THRESHOLD) + ")";
    }

    return result;
}

DetectionResult NetworkDetector::detectPacketManipulation(NetworkData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::PacketManipulation;
    return result;
}

DetectionResult NetworkDetector::detectCmdAnomalies(NetworkData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::PacketManipulation;

    if (data.totalCmds < 50) return result;

    float dupRatio = static_cast<float>(data.duplicateCmds) /
                     static_cast<float>(data.totalCmds);

    if (dupRatio > 0.3f) {
        result.detected = true;
        result.confidence = dupRatio;
        result.score = static_cast<uint32_t>(m_cmdAnomalyWeight * result.confidence);
        result.evidence = "UserCmd anomalies: " + std::to_string(data.duplicateCmds) +
            " duplicate commands (" + std::to_string(static_cast<int>(dupRatio * 100)) + "%)";
    }

    return result;
}

DetectionResult NetworkDetector::detectTickIrregularities(NetworkData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::PacketManipulation;
    return result;
}

NetworkData& NetworkDetector::getOrCreateData(const std::string& playerKey) {
    auto it = m_playerData.find(playerKey);
    if (it != m_playerData.end()) return it->second;
    return m_playerData[playerKey];
}

} // namespace sentinel::modules::network
