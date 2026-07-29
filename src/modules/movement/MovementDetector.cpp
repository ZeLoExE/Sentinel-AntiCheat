// ============================================================================
// Sentinel AntiCheat - Movement Detector Implementation
// ============================================================================
// Analyzes movement patterns to detect BunnyHop scripts, speed hacks,
// auto-strafe, ground strafe, duck scripts, and air movement anomalies.
// ============================================================================

#include "MovementDetector.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace sentinel::modules::movement {

MovementDetector::MovementDetector(IPlayerManager* playerMgr, ILogger* logger)
    : m_playerManager(playerMgr)
    , m_logger(logger)
{
    m_config.name = m_name;
    m_config.description = m_desc;
    m_config.enabled = true;
    m_config.riskScore = 25;
    m_config.threshold = 0.5f;
    m_config.minSamples = 20;
    m_config.cooldownMs = 5000;
}

const std::string& MovementDetector::name() const noexcept { return m_name; }
const std::string& MovementDetector::description() const noexcept { return m_desc; }
DetectionType MovementDetector::detectionType() const noexcept {
    return static_cast<DetectionType>(0x0100);
}

DetectionResult MovementDetector::detect(const std::string& playerKey, float deltaTime) {
    std::lock_guard lock(m_mutex);
    auto& data = getOrCreateData(playerKey);

    DetectionResult result;
    result.moduleName = m_name;
    result.type = detectionType();

    if (!m_config.enabled || data.sampleCount < m_config.minSamples) {
        return result;
    }

    // Run all detection subroutines and find the most significant
    DetectionResult candidates[] = {
        detectBHop(data),
        detectSpeedHack(data),
        detectAutoStrafe(data),
        detectGroundStrafe(data),
        detectDuckScript(data),
        detectAirMovement(data)
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

void MovementDetector::reset(const std::string& playerKey) {
    std::lock_guard lock(m_mutex);
    m_playerData.erase(playerKey);
}

void MovementDetector::resetAll() {
    std::lock_guard lock(m_mutex);
    m_playerData.clear();
}

const DetectorConfig& MovementDetector::config() const noexcept { return m_config; }

void MovementDetector::configure(const DetectorConfig& cfg) { m_config = cfg; }
bool MovementDetector::requiresFrameUpdate() const noexcept { return false; }
void MovementDetector::onFrameUpdate(float deltaTime) {}

float MovementDetector::currentSuspicion(const std::string& playerKey) const noexcept {
    std::lock_guard lock(m_mutex);
    auto it = m_playerData.find(playerKey);
    if (it == m_playerData.end()) return 0.0f;
    return it->second.bhopScore;
}

void MovementDetector::recordMovement(const std::string& playerKey, const Vector3& origin,
                                        const Vector3& velocity, bool onGround,
                                        bool ducking, const QAngle& viewAngles,
                                        uint32_t buttons) {
    std::lock_guard lock(m_mutex);
    auto& data = getOrCreateData(playerKey);

    data.positions.push_back(origin);
    data.velocities.push_back(velocity);

    float speed = velocity.length2d();
    data.speeds.push_back(speed);
    data.onGround.push_back(onGround);
    data.ducking.push_back(ducking);

    // Track max speeds
    if (speed > data.maxSpeed) data.maxSpeed = speed;
    if (onGround && speed > data.maxGroundSpeed) data.maxGroundSpeed = speed;

    // Air movement tracking
    if (!onGround) {
        data.airTicks++;
        if (speed > data.maxAirSpeed) data.maxAirSpeed = speed;
    }

    // BunnyHop detection
    if (data.onGround.size() >= 2) {
        bool wasOnGround = data.onGround[data.onGround.size() - 2];
        bool isOnGroundNow = data.onGround.back();

        // Player just landed
        if (!wasOnGround && isOnGroundNow) {
            data.totalJumps++;

            // Check if jump button was pressed immediately on landing
            bool jumpPressed = (buttons & (1 << 1)) != 0;  // IN_JUMP
            if (jumpPressed) {
                data.perfectBhops++;
            }
        }
    }

    // Trim history
    if (data.positions.size() > MovementData::MAX_HISTORY) {
        data.positions.pop_front();
        data.velocities.pop_front();
        data.speeds.pop_front();
        data.onGround.pop_front();
        data.ducking.pop_front();
    }

    data.sampleCount++;
    data.lastUpdate = std::chrono::steady_clock::now();
}

// ── Detection Subroutines ────────────────────────────────────────────────

DetectionResult MovementDetector::detectBHop(MovementData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::BHopScript;

    if (data.totalJumps < 10) return result;

    data.bhopRatio = data.totalJumps > 0
        ? static_cast<float>(data.perfectBhops) / static_cast<float>(data.totalJumps)
        : 0.0f;

    // Legitimate players achieve ~20-60% bhop ratio
    // Scripts achieve 80%+ consistently
    if (data.bhopRatio > 0.80f) {
        result.detected = true;
        result.confidence = std::min(1.0f, (data.bhopRatio - 0.8f) * 5.0f);
        result.score = static_cast<uint32_t>(m_bhopWeight * result.confidence);
        result.evidence = "BHop script: " + std::to_string(static_cast<int>(data.bhopRatio * 100)) +
            "% perfect hops (" + std::to_string(data.perfectBhops) + "/" +
            std::to_string(data.totalJumps) + ")";
    }

    return result;
}

DetectionResult MovementDetector::detectSpeedHack(MovementData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::SpeedHack;

    // CS 1.6 max ground speed is ~320 units/sec
    // Speed hacks push beyond this significantly
    if (data.maxSpeed > MovementData::MAX_GROUND_SPEED * 1.5f) {
        result.detected = true;
        result.confidence = std::min(1.0f, (data.maxSpeed - MovementData::MAX_GROUND_SPEED) /
                                             MovementData::MAX_GROUND_SPEED);
        result.score = static_cast<uint32_t>(m_speedHackWeight * result.confidence);
        result.evidence = "Speed hack: max speed " +
            std::to_string(static_cast<int>(data.maxSpeed)) +
            " (limit: " + std::to_string(static_cast<int>(MovementData::MAX_GROUND_SPEED)) + ")";
    }

    return result;
}

DetectionResult MovementDetector::detectAutoStrafe(MovementData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::AutoStrafe;

    // Auto-strafe scripts maintain perfect strafe synchronization
    // This shows as unnaturally consistent air acceleration
    if (data.airTicks > 30 && data.maxAirSpeed > MovementData::MAX_AIR_SPEED * 0.9f) {
        float speedConsistency = 0.0f;
        if (!data.airSpeeds.empty()) {
            float sum = std::accumulate(data.airSpeeds.begin(), data.airSpeeds.end(), 0.0f);
            float mean = sum / static_cast<float>(data.airSpeeds.size());
            float sqSum = 0.0f;
            for (float s : data.airSpeeds) sqSum += (s - mean) * (s - mean);
            speedConsistency = std::sqrt(sqSum / static_cast<float>(data.airSpeeds.size()));
        }

        if (speedConsistency < 10.0f && data.avgAirAccel > 100.0f) {
            result.detected = true;
            result.confidence = std::min(1.0f, (10.0f - speedConsistency) / 10.0f);
            result.score = static_cast<uint32_t>(m_autoStrafeWeight * result.confidence);
            result.evidence = "Auto-strafe: consistent air acceleration";
        }
    }

    return result;
}

DetectionResult MovementDetector::detectGroundStrafe(MovementData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::GroundStrafe;
    return result;  // Placeholder - needs more sophisticated analysis
}

DetectionResult MovementDetector::detectDuckScript(MovementData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::DuckScript;

    if (data.duckToggleCount < 10) return result;

    // Duck scripts perform perfectly-timed duck toggles
    data.duckAvgDuration = data.duckDurations.empty() ? 0.0f :
        std::accumulate(data.duckDurations.begin(), data.duckDurations.end(), 0.0f) /
        static_cast<float>(data.duckDurations.size());

    float duckStdDev = 0.0f;
    if (data.duckDurations.size() > 5) {
        float sqSum = 0.0f;
        for (uint32_t d : data.duckDurations) {
            sqSum += (static_cast<float>(d) - data.duckAvgDuration) *
                     (static_cast<float>(d) - data.duckAvgDuration);
        }
        duckStdDev = std::sqrt(sqSum / static_cast<float>(data.duckDurations.size()));
    }

    // Perfectly consistent duck timing suggests a script
    if (data.duckToggleCount > 20 && duckStdDev < 5.0f && data.duckAvgDuration < 100.0f) {
        result.detected = true;
        result.confidence = std::min(1.0f, (5.0f - duckStdDev) / 5.0f);
        result.score = static_cast<uint32_t>(m_duckScriptWeight * result.confidence);
        result.evidence = "Duck script: " + std::to_string(data.duckToggleCount) +
            " toggles, avg " + std::to_string(static_cast<int>(data.duckAvgDuration)) + "ms";
    }

    return result;
}

DetectionResult MovementDetector::detectAirMovement(MovementData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::AutoStrafe;
    return result;
}

MovementData& MovementDetector::getOrCreateData(const std::string& playerKey) {
    auto it = m_playerData.find(playerKey);
    if (it != m_playerData.end()) return it->second;
    return m_playerData[playerKey];
}

} // namespace sentinel::modules::movement
