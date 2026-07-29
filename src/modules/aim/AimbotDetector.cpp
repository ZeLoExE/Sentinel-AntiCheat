// ============================================================================
// Sentinel AntiCheat - Aimbot Detector Implementation
// ============================================================================
// Analyzes aim angles for aim assistance signs using statistical methods.
// Uses accumulated evidence over time to distinguish skill from cheating.
// ============================================================================

#include "AimbotDetector.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace sentinel::modules::aim {

AimbotDetector::AimbotDetector(IPlayerManager* playerMgr, ILogger* logger)
    : m_playerManager(playerMgr)
    , m_logger(logger)
{
    m_config.name = m_name;
    m_config.description = m_desc;
    m_config.enabled = true;
    m_config.riskScore = 25;
    m_config.threshold = 0.5f;
    m_config.minSamples = 30;
    m_config.cooldownMs = 5000;
}

// ── IDetector Implementation ─────────────────────────────────────────────

const std::string& AimbotDetector::name() const noexcept { return m_name; }
const std::string& AimbotDetector::description() const noexcept { return m_desc; }
DetectionType AimbotDetector::detectionType() const noexcept {
    return DetectionType::Aimbot;
}

DetectionResult AimbotDetector::detect(const std::string& playerKey, float deltaTime) {
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
        detectSnapAim(data),
        detectPerfectTracking(data),
        detectSilentAim(data),
        detectTriggerBot(data),
        detectMicroCorrections(data),
        detectRecoilCompensation(data),
        detectSmoothingAnomaly(data)
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

void AimbotDetector::reset(const std::string& playerKey) {
    std::lock_guard lock(m_mutex);
    m_playerData.erase(playerKey);
}

void AimbotDetector::resetAll() {
    std::lock_guard lock(m_mutex);
    m_playerData.clear();
}

const DetectorConfig& AimbotDetector::config() const noexcept { return m_config; }

void AimbotDetector::configure(const DetectorConfig& cfg) {
    m_config = cfg;
}

bool AimbotDetector::requiresFrameUpdate() const noexcept { return false; }

void AimbotDetector::onFrameUpdate(float deltaTime) {}

float AimbotDetector::currentSuspicion(const std::string& playerKey) const noexcept {
    std::lock_guard lock(m_mutex);
    auto it = m_playerData.find(playerKey);
    if (it == m_playerData.end()) return 0.0f;
    return it->second.aimbotScore;
}

void AimbotDetector::recordAngle(const std::string& playerKey, const QAngle& viewAngle,
                                   const QAngle& punchAngle) {
    std::lock_guard lock(m_mutex);
    auto& data = getOrCreateData(playerKey);

    // Calculate angle delta from last known angle
    if (!data.angles.empty()) {
        QAngle delta = viewAngle - data.angles.back();

        // Normalize angles to [-180, 180]
        auto normalizeAngle = [](float& a) {
            while (a > 180.0f) a -= 360.0f;
            while (a < -180.0f) a += 360.0f;
        };
        normalizeAngle(delta.pitch);
        normalizeAngle(delta.yaw);

        float magnitude = delta.length();

        data.angleDeltas.push_back(delta);
        data.angularVelocities.push_back(magnitude);

        // Calculate acceleration
        if (data.angularVelocities.size() >= 2) {
            float accel = std::abs(magnitude - data.angularVelocities.back());
            data.accelerations.push_back(accel);
        }

        // Snap detection
        if (magnitude > AimData::SNAP_THRESHOLD) {
            data.snapMagnitudes.push_back(magnitude);
            data.snapCount++;
            if (magnitude > data.maxSnapMagnitude) {
                data.maxSnapMagnitude = magnitude;
            }
        }
    }

    data.angles.push_back(viewAngle);

    // Trim history
    if (data.angles.size() > AimData::MAX_HISTORY) {
        data.angles.pop_front();
        data.angleDeltas.pop_front();
        data.angularVelocities.pop_front();
    }

    data.sampleCount++;
    data.lastUpdate = std::chrono::steady_clock::now();
}

void AimbotDetector::recordKill(const std::string& playerKey, const QAngle& aimAngleAtKill,
                                  float distance, bool headshot) {
    std::lock_guard lock(m_mutex);
    auto& data = getOrCreateData(playerKey);
    // Store for tracking analysis
}

void AimbotDetector::recordShot(const std::string& playerKey, float timeSinceTargetVisible) {
    std::lock_guard lock(m_mutex);
    auto& data = getOrCreateData(playerKey);

    if (timeSinceTargetVisible > 0.0f) {
        data.reactionTimes.push_back(timeSinceTargetVisible);
        if (timeSinceTargetVisible < AimData::IMPOSSIBLE_REACTION) {
            data.impossibleReactions++;
        }

        // Calculate average
        float sum = std::accumulate(data.reactionTimes.begin(), data.reactionTimes.end(), 0.0f);
        data.avgReactionMs = sum / static_cast<float>(data.reactionTimes.size());
    }
}

void AimbotDetector::setTargetPosition(const std::string& playerKey,
                                         const Vector3& targetOrigin, float targetRadius) {
    // Would be used for tracking accuracy analysis
    // Requires world state to determine where the enemy actually is
}

// ── Detection Subroutines ────────────────────────────────────────────────

DetectionResult AimbotDetector::detectSnapAim(AimData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::Aimbot;

    if (data.snapCount < 3) return result;

    // Calculate average snap magnitude
    float sum = std::accumulate(data.snapMagnitudes.begin(), data.snapMagnitudes.end(), 0.0f);
    float avgSnap = sum / static_cast<float>(data.snapMagnitudes.size());
    float snapRatio = static_cast<float>(data.snapCount) / static_cast<float>(data.sampleCount);

    // Legitimate players may have occasional large flicks
    // Cheaters have consistent, large snaps (no in-between frames)
    if (snapRatio > 0.05f && avgSnap > 30.0f) {
        result.detected = true;
        result.confidence = std::min(1.0f, snapRatio * 2.0f);
        result.score = static_cast<uint32_t>(m_snapAimWeight * result.confidence);
        result.evidence = "Snap aim detected: " + std::to_string(data.snapCount) +
            " snaps, avg " + std::to_string(static_cast<int>(avgSnap)) + " degrees";
    }

    return result;
}

DetectionResult AimbotDetector::detectPerfectTracking(AimData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::PerfectTracking;

    if (data.totalTrackingFrames < 30) return result;

    float perfectRatio = static_cast<float>(data.perfectFrames) /
                         static_cast<float>(data.totalTrackingFrames);

    // Perfect tracking: cheaters maintain < 1° error for extended periods
    if (perfectRatio > 0.8f && data.avgTrackingError < 2.0f) {
        result.detected = true;
        result.confidence = perfectRatio;
        result.score = static_cast<uint32_t>(m_perfectTrackWeight * result.confidence);
        result.evidence = "Perfect tracking: " +
            std::to_string(static_cast<int>(perfectRatio * 100)) +
            "% frames within 1 degree";
    }

    return result;
}

DetectionResult AimbotDetector::detectSilentAim(AimData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::SilentAim;

    if (data.angleDeltas.size() < 10) return result;

    // Silent aim shows as instant angle changes without intermediate frames
    uint32_t instantSnaps = 0;
    for (const auto& mag : data.snapMagnitudes) {
        if (mag > 45.0f) instantSnaps++;
    }

    float instantRatio = static_cast<float>(instantSnaps) /
                         std::max(1.0f, static_cast<float>(data.snapMagnitudes.size()));

    if (instantRatio > 0.3f && data.snapCount > 5) {
        result.detected = true;
        result.confidence = std::min(1.0f, instantRatio * 1.5f);
        result.score = static_cast<uint32_t>(m_silentAimWeight * result.confidence);
        result.evidence = "Silent aim suspected: " + std::to_string(instantSnaps) +
            " instant angle changes > 45 degrees";
    }

    return result;
}

DetectionResult AimbotDetector::detectTriggerBot(AimData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::TriggerBot;

    if (data.reactionTimes.size() < 5) return result;

    float avgReact = data.avgReactionMs;
    int impossibleCount = data.impossibleReactions;

    // Humans average 200-300ms reaction time
    // < 100ms is suspicious for sustained play
    if (avgReact < 100.0f && impossibleCount > 3) {
        result.detected = true;
        result.confidence = std::min(1.0f, (100.0f - avgReact) / 100.0f);
        result.score = static_cast<uint32_t>(m_triggerBotWeight * result.confidence);
        result.evidence = "Trigger bot suspected: avg reaction " +
            std::to_string(static_cast<int>(avgReact)) + "ms (" +
            std::to_string(impossibleCount) + " sub-100ms)";
    }

    return result;
}

DetectionResult AimbotDetector::detectMicroCorrections(AimData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::MicroCorrection;

    if (data.correctionCount < 5) return result;

    // Micro-corrections are tiny adjustments that aimbots make
    // when snapping to a new target
    float corrRatio = static_cast<float>(data.correctionCount) /
                      static_cast<float>(data.angleDeltas.size());

    if (corrRatio > 0.1f) {
        result.detected = true;
        result.confidence = std::min(1.0f, corrRatio * 3.0f);
        result.score = static_cast<uint32_t>(m_microCorrectionWeight * result.confidence);
        result.evidence = "Micro-corrections detected: " +
            std::to_string(data.correctionCount) + " adjustments";
    }

    return result;
}

DetectionResult AimbotDetector::detectRecoilCompensation(AimData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::RecoilCompensation;
    // Implementation would analyze punch angle vs view angle correlation
    return result;
}

DetectionResult AimbotDetector::detectSmoothingAnomaly(AimData& data) {
    DetectionResult result;
    result.moduleName = m_name;
    result.type = DetectionType::AimSmoothingAnomaly;

    if (data.angularVelocities.size() < 30) return result;

    // Calculate variance in angular velocity
    float sum = std::accumulate(data.angularVelocities.begin(), data.angularVelocities.end(), 0.0f);
    float mean = sum / static_cast<float>(data.angularVelocities.size());
    float sqSum = 0.0f;
    for (float v : data.angularVelocities) {
        sqSum += (v - mean) * (v - mean);
    }
    float variance = sqSum / static_cast<float>(data.angularVelocities.size());
    float stdDev = std::sqrt(variance);

    // Aimbot smoothing produces unnaturally consistent angular velocities
    if (stdDev < 0.5f && mean > 0.5f) {
        result.detected = true;
        result.confidence = std::min(1.0f, (0.5f - stdDev) * 2.0f);
        result.score = static_cast<uint32_t>(m_smoothingAnomalyWeight * result.confidence);
        result.evidence = "Smoothing anomaly: angular velocity stddev = " +
            std::to_string(stdDev);
    }

    return result;
}

AimData& AimbotDetector::getOrCreateData(const std::string& playerKey) {
    auto it = m_playerData.find(playerKey);
    if (it != m_playerData.end()) return it->second;
    return m_playerData[playerKey];
}

} // namespace sentinel::modules::aim
