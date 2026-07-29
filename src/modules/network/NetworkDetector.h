// ============================================================================
// Sentinel AntiCheat - Network Detector
// ============================================================================
// Analyzes network behavior for signs of cheating.
// Detects: FakeLag (choke manipulation), packet manipulation,
//          command flooding, UserCmd anomalies, and tick irregularities.
// ============================================================================

#pragma once
#include "core/IDetector.h"
#include "core/IPlayerManager.h"
#include "core/ILogger.h"
#include <unordered_map>
#include <deque>
#include <cmath>

namespace sentinel::modules::network {

/// Per-player network tracking data
struct NetworkData {
    // Latency/Choke
    std::deque<float>   latency;
    std::deque<float>   packetLoss;
    std::deque<uint32_t> chokeAmount;
    float               avgLatency = 0.0f;
    float               avgPacketLoss = 0.0f;
    uint32_t            avgChoke = 0;

    // FakeLag detection
    std::deque<uint32_t> chokePatterns;     // Choke values over time
    uint32_t            highChokeFrames = 0;
    uint32_t            totalChokeFrames = 0;
    float               chokeRatio = 0.0f;
    bool                fakeLagSuspected = false;
    uint32_t            fakeLagConsistency = 0; // How consistent the lag pattern is

    // Command flood
    std::deque<uint32_t> commandsPerTick;
    uint32_t            maxCommandsPerTick = 0;
    double              avgCommandsPerTick = 0.0;
    uint32_t            floodWarnings = 0;

    // UserCmd anomalies
    std::deque<uint32_t> cmdButtons;
    uint32_t            invalidCmds = 0;
    uint32_t            duplicateCmds = 0;
    uint32_t            totalCmds = 0;

    // Movement prediction mismatches
    std::deque<float>   predictionErrors;
    float               avgPredictionError = 0.0f;
    float               maxPredictionError = 0.0f;

    // Tick analysis
    std::deque<uint32_t> tickDeltas;
    uint32_t            tickIrregularities = 0;
    uint32_t            totalTicks = 0;

    // Detection scores
    float               fakeLagScore = 0.0f;
    float               packetScore = 0.0f;
    float               floodScore = 0.0f;
    uint32_t            sampleCount = 0;

    std::chrono::steady_clock::time_point lastUpdate;
    std::chrono::steady_clock::time_point lastDetection;

    static constexpr size_t MAX_HISTORY = 300;
    static constexpr float  FAKELAG_CHOKE_THRESHOLD = 80.0f;  // % choke
    static constexpr uint32_t FAKELAG_MIN_FRAMES = 10;
    static constexpr uint32_t COMMAND_FLOOD_THRESHOLD = 10; // Cmds per tick
    static constexpr size_t MIN_SAMPLES = 30;
};

/// Network Detector
class NetworkDetector final : public IDetector {
public:
    explicit NetworkDetector(IPlayerManager* playerMgr, ILogger* logger);

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

    /// Record network stats for a player
    void recordNetworkStats(
        const std::string& playerKey,
        float latency,
        float packetLoss,
        uint32_t choke
    );

    /// Record a UserCmd from a player
    void recordUserCmd(
        const std::string& playerKey,
        const UserCmdData& cmd
    );

    /// Record a packet from a player
    void recordPacket(
        const std::string& playerKey,
        uint32_t sequenceNumber,
        uint32_t tickCount
    );

private:
    // ── Detection Subroutines ───────────────────────────────────────────

    /// Detect FakeLag (intentional choke manipulation)
    [[nodiscard]] DetectionResult detectFakeLag(NetworkData& data);

    /// Detect command flooding
    [[nodiscard]] DetectionResult detectCommandFlood(NetworkData& data);

    /// Detect packet manipulation
    [[nodiscard]] DetectionResult detectPacketManipulation(NetworkData& data);

    /// Detect UserCmd anomalies
    [[nodiscard]] DetectionResult detectCmdAnomalies(NetworkData& data);

    /// Detect tick irregularities
    [[nodiscard]] DetectionResult detectTickIrregularities(NetworkData& data);

    /// Get or create network data for a player
    NetworkData& getOrCreateData(const std::string& playerKey);

    IPlayerManager*                     m_playerManager;
    ILogger*                            m_logger;
    DetectorConfig                      m_config;

    std::string m_name = "NetworkDetector";
    std::string m_desc = "Detects network cheats including FakeLag, packet manipulation, and command flooding";

    std::unordered_map<std::string, NetworkData> m_playerData;
    mutable std::mutex                  m_mutex;

    // Score weights
    uint32_t m_fakeLagWeight            = 15;
    uint32_t m_cmdFloodWeight           = 10;
    uint32_t m_packetWeight             = 20;
    uint32_t m_cmdAnomalyWeight         = 10;
    uint32_t m_tickIrregularityWeight   = 10;
};

} // namespace sentinel::modules::network
