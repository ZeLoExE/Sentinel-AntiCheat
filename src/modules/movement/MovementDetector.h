// ============================================================================
// Sentinel AntiCheat - Movement Detector
// ============================================================================
// Analyzes player movement patterns for signs of cheating.
// Detects: BunnyHop scripts, speed hacks, auto-strafe, ground strafe,
//          duck scripts, air movement anomalies, and acceleration hacks.
// ============================================================================

#pragma once
#include "core/IDetector.h"
#include "core/IPlayerManager.h"
#include "core/ILogger.h"
#include <unordered_map>
#include <deque>
#include <cmath>

namespace sentinel::modules::movement {

/// Per-player movement tracking data
struct MovementData {
    // Position/Orientation history
    std::deque<Vector3> positions;
    std::deque<Vector3> velocities;
    std::deque<float>   speeds;
    std::deque<float>   accelerations;
    std::deque<bool>    onGround;       // Was player on ground?
    std::deque<bool>    ducking;

    // BunnyHop analysis
    uint32_t            totalJumps = 0;
    uint32_t            perfectBhops = 0;   // Frames landed + jump immediately
    float               bhopRatio = 0.0f;   // Perfect / Total jumps
    std::deque<uint32_t> jumpTimings;        // Tick delays between jumps

    // Speed analysis
    float               maxSpeed = 0.0f;
    float               avgSpeed = 0.0f;
    float               maxGroundSpeed = 0.0f;  // Speed while on ground
    uint32_t            speedHackFrames = 0;
    uint32_t            totalGroundFrames = 0;

    // Strafe analysis
    std::deque<float>   strafeAngles;        // Angle changes during strafe
    std::deque<float>   strafeSyncValues;    // How synchronized strafe is
    float               strafeSyncAvg = 0.0f;
    bool                isStrafing = false;

    // Duck analysis
    uint32_t            duckToggleCount = 0;
    std::deque<uint32_t> duckDurations;       // How long duck held
    float               duckAvgDuration = 0.0f;

    // Air movement
    std::deque<float>   airSpeeds;
    float               maxAirSpeed = 0.0f;
    float               avgAirAccel = 0.0f;
    uint32_t            airTicks = 0;

    // Detection scores
    float               bhopScore = 0.0f;
    float               speedHackScore = 0.0f;
    float               strafeScore = 0.0f;
    float               duckScore = 0.0f;
    uint32_t            sampleCount = 0;

    std::chrono::steady_clock::time_point lastUpdate;
    std::chrono::steady_clock::time_point lastDetection;

    static constexpr size_t MAX_HISTORY = 200;
    static constexpr float  MAX_GROUND_SPEED = 320.0f;   // Max CS 1.6 ground speed
    static constexpr float  MAX_AIR_SPEED = 400.0f;       // Max CS 1.6 air speed
    static constexpr float  BHOP_WINDOW_MS = 50.0f;       // Window for "perfect" bhop
    static constexpr size_t MIN_SAMPLES = 20;
};

/// Movement Detector
class MovementDetector final : public IDetector {
public:
    explicit MovementDetector(IPlayerManager* playerMgr, ILogger* logger);

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

    /// Record player movement for a frame
    void recordMovement(
        const std::string& playerKey,
        const Vector3& origin,
        const Vector3& velocity,
        bool onGround,
        bool ducking,
        const QAngle& viewAngles,
        uint32_t buttons
    );

private:
    // ── Detection Subroutines ───────────────────────────────────────────

    /// Detect BunnyHop scripting
    [[nodiscard]] DetectionResult detectBHop(MovementData& data);

    /// Detect speed hacking
    [[nodiscard]] DetectionResult detectSpeedHack(MovementData& data);

    /// Detect auto-strafe (perfect strafe synchronization)
    [[nodiscard]] DetectionResult detectAutoStrafe(MovementData& data);

    /// Detect ground strafe anomalies
    [[nodiscard]] DetectionResult detectGroundStrafe(MovementData& data);

    /// Detect duck script
    [[nodiscard]] DetectionResult detectDuckScript(MovementData& data);

    /// Detect air movement anomalies
    [[nodiscard]] DetectionResult detectAirMovement(MovementData& data);

    /// Get or create movement data for a player
    MovementData& getOrCreateData(const std::string& playerKey);

    IPlayerManager*                     m_playerManager;
    ILogger*                            m_logger;
    DetectorConfig                      m_config;

    std::string m_name = "MovementDetector";
    std::string m_desc = "Detects movement cheats including BHop scripts, speed hacks, auto-strafe, and duck scripts";

    std::unordered_map<std::string, MovementData> m_playerData;
    mutable std::mutex                  m_mutex;

    // Score weights
    uint32_t m_bhopWeight               = 10;
    uint32_t m_speedHackWeight          = 30;
    uint32_t m_autoStrafeWeight         = 15;
    uint32_t m_groundStrafeWeight       = 10;
    uint32_t m_duckScriptWeight         = 8;
    uint32_t m_airMovementWeight        = 10;
};

} // namespace sentinel::modules::movement
