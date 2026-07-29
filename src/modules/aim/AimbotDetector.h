// ============================================================================
// Sentinel AntiCheat - Aimbot Detector
// ============================================================================
// Analyzes player aim behavior for signs of aim assistance.
// Detects: Snap aim, perfect tracking, silent aim, trigger bot,
//          micro-corrections, unnatural smoothing, and recoil compensation.
// ============================================================================

#pragma once
#include "core/IDetector.h"
#include "core/IPlayerManager.h"
#include "core/ILogger.h"
#include <unordered_map>
#include <deque>
#include <cmath>

namespace sentinel::modules::aim {

/// Per-player aim tracking data
struct AimData {
    // Raw angle history
    std::deque<QAngle>  angles;           // Last N view angles
    std::deque<QAngle>  angleDeltas;      // Angle changes per frame
    std::deque<float>   angularVelocities; // Speed of angle changes
    std::deque<float>   accelerations;     // Acceleration of angle changes

    // Snap detection
    std::deque<float>   snapMagnitudes;   // Large single-frame angle changes
    uint32_t            snapCount = 0;
    float               maxSnapMagnitude = 0.0f;

    // Tracking analysis
    std::deque<float>   trackingErrors;   // Distance from target center
    float               avgTrackingError  = 0.0f;
    float               minTrackingError  = 999.0f;
    uint32_t            perfectFrames     = 0;   // Frames with < 1° error
    uint32_t            totalTrackingFrames = 0;

    // Smoothing
    std::deque<float>   smoothingValues;  // Measured smoothing factor
    float               avgSmoothing      = 0.0f;

    // Reaction time
    std::deque<float>   reactionTimes;    // Time to acquire target (ms)
    float               avgReactionMs     = 0.0f;
    uint32_t            impossibleReactions = 0; // < 100ms reactions

    // Flick analysis
    std::deque<float>   flickSpeeds;      // Degrees per second during flicks
    float               maxFlickSpeed     = 0.0f;
    uint32_t            perfectFlicks     = 0;   // Flicks landing exactly on target

    // Micro-corrections
    std::deque<float>   microCorrections; // Tiny angle adjustments near target
    uint32_t            correctionCount   = 0;

    // Detection scores
    float               aimbotScore      = 0.0f;
    float               silentAimScore   = 0.0f;
    float               triggerScore     = 0.0f;
    uint32_t            sampleCount      = 0;

    // Time tracking
    std::chrono::steady_clock::time_point lastUpdate;
    std::chrono::steady_clock::time_point lastDetection;

    static constexpr size_t MAX_HISTORY = 120;   // ~2 seconds at 64 tick
    static constexpr float  SNAP_THRESHOLD = 15.0f;  // Degrees per frame
    static constexpr float  PERFECT_TRACK_THRESHOLD = 1.0f; // Degrees
    static constexpr float  IMPOSSIBLE_REACTION = 100.0f; // Milliseconds
    static constexpr size_t MIN_SAMPLES = 30;
};

/// Aimbot Detector Module
/// Analyzes multiple aspects of player aim to detect various aim assist types.
/// Uses statistical analysis over time to distinguish skill from cheating.
class AimbotDetector final : public IDetector {
public:
    explicit AimbotDetector(IPlayerManager* playerMgr, ILogger* logger);

    // ── IDetector Implementation ─────────────────────────────────────────
    [[nodiscard]] const std::string& name() const noexcept override;
    [[nodiscard]] const std::string& description() const noexcept override;
    [[nodiscard]] DetectionType detectionType() const noexcept override;

    [[nodiscard]] DetectionResult detect(
        const std::string& playerKey,
        float deltaTime
    ) override;

    void reset(const std::string& playerKey) override;
    void resetAll() override;

    [[nodiscard]] const DetectorConfig& config() const noexcept override;
    void configure(const DetectorConfig& cfg) override;

    [[nodiscard]] bool requiresFrameUpdate() const noexcept override;
    void onFrameUpdate(float deltaTime) override;

    [[nodiscard]] float currentSuspicion(
        const std::string& playerKey
    ) const noexcept override;

    // ── Analysis Methods ─────────────────────────────────────────────────

    /// Record a view angle update for a player
    void recordAngle(
        const std::string& playerKey,
        const QAngle& viewAngle,
        const QAngle& punchAngle
    );

    /// Record a kill for tracking analysis
    void recordKill(
        const std::string& playerKey,
        const QAngle& aimAngleAtKill,
        float distance,
        bool headshot
    );

    /// Record a shot for trigger bot analysis
    void recordShot(
        const std::string& playerKey,
        float timeSinceTargetVisible
    );

    /// Set target position for tracking accuracy
    void setTargetPosition(
        const std::string& playerKey,
        const Vector3& targetOrigin,
        float targetRadius
    );

private:
    // ── Detection Subroutines ────────────────────────────────────────────

    /// Detect aim assist based on snap analysis
    [[nodiscard]] DetectionResult detectSnapAim(AimData& data);

    /// Detect perfect tracking (unnaturally smooth aim on target)
    [[nodiscard]] DetectionResult detectPerfectTracking(AimData& data);

    /// Detect silent aim (instantaneous angle changes without animation)
    [[nodiscard]] DetectionResult detectSilentAim(AimData& data);

    /// Detect trigger bot (unnaturally fast reaction when target is visible)
    [[nodiscard]] DetectionResult detectTriggerBot(AimData& data);

    /// Detect micro-corrections (robotic micro-adjustments)
    [[nodiscard]] DetectionResult detectMicroCorrections(AimData& data);

    /// Detect unnatural recoil compensation
    [[nodiscard]] DetectionResult detectRecoilCompensation(AimData& data);

    /// Analyze smoothing for anomalies
    [[nodiscard]] DetectionResult detectSmoothingAnomaly(AimData& data);

    /// Get or create aim data for a player
    AimData& getOrCreateData(const std::string& playerKey);

    IPlayerManager*                 m_playerManager;
    ILogger*                        m_logger;
    DetectorConfig                  m_config;

    std::string                     m_name = "AimbotDetector";
    std::string                     m_desc = "Detects aim assistance including aimbot, silent aim, trigger bot, and unnatural tracking";

    std::unordered_map<std::string, AimData> m_playerData;
    mutable std::mutex              m_mutex;

    // Score weights (configurable)
    uint32_t m_snapAimWeight        = 25;
    uint32_t m_silentAimWeight      = 35;
    uint32_t m_perfectTrackWeight   = 20;
    uint32_t m_triggerBotWeight     = 15;
    uint32_t m_microCorrectionWeight = 15;
    uint32_t m_recoilWeight         = 10;
    uint32_t m_smoothingAnomalyWeight = 10;
};

} // namespace sentinel::modules::aim
