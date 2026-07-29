// ============================================================================
// Sentinel AntiCheat - Behavior Detector
// ============================================================================
// Analyzes player behavior patterns for signs of cheating.
// Detects: Impossible consistency, inhuman movement patterns,
//          repeated identical patterns, kill streak anomalies,
//          and cross-round statistical abnormalities.
// ============================================================================

#pragma once
#include "core/IDetector.h"
#include "core/IPlayerManager.h"
#include "core/ILogger.h"
#include <unordered_map>
#include <deque>
#include <cmath>

namespace sentinel::modules::behavior {

/// Per-player behavior tracking data
struct BehaviorData {
    // Kill analysis
    std::deque<uint32_t> killStreaks;
    uint32_t            maxKillStreak = 0;
    uint32_t            avgKillStreak = 0;
    uint32_t            totalKills = 0;
    uint32_t            totalDeaths = 0;

    // Headshot analysis
    std::deque<uint32_t> killsPerRound;
    std::deque<uint32_t> headshotsPerRound;
    float               headshotRatio = 0.0f;
    float               roundKdRatio = 0.0f;

    // Distance analysis
    std::deque<float>   killDistances;
    float               avgKillDistance = 0.0f;
    float               minKillDistance = 999.0f;
    float               maxKillDistance = 0.0f;

    // Consistency analysis
    std::deque<float>   roundScores;         // Performance per round
    float               scoreStdDev = 0.0f;  // Standard deviation of performance
    float               scoreMean = 0.0f;
    float               consistencyRatio = 0.0f; // Mean / StdDev

    // Pattern analysis
    std::deque<uint32_t> aimPatternHashes;   // Hashes of aim movement patterns
    uint32_t            repeatedPatterns = 0;
    uint32_t            totalPatterns = 0;

    // Weapon statistics
    std::unordered_map<std::string, uint32_t> weaponKills;
    std::unordered_map<std::string, float> weaponAccuracy;
    float               primaryAccuracy = 0.0f;
    float               secondaryAccuracy = 0.0f;

    // Time analysis
    std::deque<std::chrono::steady_clock::time_point> killTimestamps;
    std::deque<float>   killIntervals;       // Time between kills (seconds)
    float               avgKillInterval = 0.0f;
    float               minKillInterval = 999.0f;

    // Detection scores
    float               consistencyScore = 0.0f;
    float               patternScore = 0.0f;
    float               streakScore = 0.0f;
    uint32_t            sampleCount = 0;

    std::chrono::steady_clock::time_point lastUpdate;
    std::chrono::steady_clock::time_point lastDetection;

    static constexpr size_t MAX_HISTORY = 100;
    static constexpr float  HIGH_CONSISTENCY_RATIO = 5.0f; // Very consistent
    static constexpr size_t MIN_SAMPLES = 5;   // Rounds to analyze
    static constexpr float  FAST_KILL_INTERVAL = 2.0f; // Seconds between kills
    static constexpr float  SUSPICIOUS_HS_RATIO = 0.7f; // 70%+ headshot ratio
};

/// Behavior Detector
class BehaviorDetector final : public IDetector {
public:
    explicit BehaviorDetector(IPlayerManager* playerMgr, ILogger* logger);

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

    /// Record a kill event
    void recordKill(
        const std::string& playerKey,
        const std::string& weapon,
        bool headshot,
        float distance
    );

    /// Record a death event
    void recordDeath(const std::string& playerKey);

    /// Record round end for performance tracking
    void recordRoundEnd(
        const std::string& playerKey,
        uint32_t killsInRound,
        uint32_t deathsInRound,
        uint32_t headshotsInRound
    );

    /// Record an aim pattern (hash of movement during engagement)
    void recordAimPattern(
        const std::string& playerKey,
        uint32_t patternHash
    );

private:
    // ── Detection Subroutines ───────────────────────────────────────────

    /// Detect impossible consistency (too consistent across rounds)
    [[nodiscard]] DetectionResult detectConsistency(BehaviorData& data);

    /// Detect repeated/robotic patterns
    [[nodiscard]] DetectionResult detectRepeatedPatterns(BehaviorData& data);

    /// Detect kill streak anomalies
    [[nodiscard]] DetectionResult detectStreakAnomaly(BehaviorData& data);

    /// Detect impossible headshot ratio
    [[nodiscard]] DetectionResult detectHeadshotRatio(BehaviorData& data);

    /// Analyze cross-round statistics for anomalies
    [[nodiscard]] DetectionResult detectCrossRoundAnomalies(BehaviorData& data);

    /// Get or create behavior data for a player
    BehaviorData& getOrCreateData(const std::string& playerKey);

    IPlayerManager*                     m_playerManager;
    ILogger*                            m_logger;
    DetectorConfig                      m_config;

    std::string m_name = "BehaviorDetector";
    std::string m_desc = "Detects behavioral cheats including impossible consistency, pattern repetition, and statistical anomalies";

    std::unordered_map<std::string, BehaviorData> m_playerData;
    mutable std::mutex                  m_mutex;

    // Score weights
    uint32_t m_consistencyWeight         = 20;
    uint32_t m_patternWeight             = 15;
    uint32_t m_streakWeight              = 10;
    uint32_t m_headshotWeight            = 10;
    uint32_t m_crossRoundWeight          = 15;
};

} // namespace sentinel::modules::behavior
