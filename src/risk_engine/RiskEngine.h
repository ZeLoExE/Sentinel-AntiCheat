// ============================================================================
// Sentinel AntiCheat - Risk Engine Implementation
// ============================================================================
// The brain of the anti-cheat system. Accumulates detection scores,
// calculates risk levels, and determines enforcement actions.
// Uses decay over time so skilled players aren't permanently flagged.
// ============================================================================

#pragma once
#include "core/IRiskEngine.h"
#include "core/ILogger.h"
#include <unordered_map>
#include <deque>
#include <chrono>
#include <mutex>

namespace sentinel::risk {

/// Per-player risk tracking state
struct PlayerRiskState {
    RiskScore               current;
    std::deque<DetectionEvent> recentEvents;
    std::deque<RiskScore>   history;
    std::chrono::system_clock::time_point lastActivity;
    uint32_t                totalDetections = 0;
    float                   scoreDecay      = 0.0f;  // Decay per second
};

/// Risk Engine Implementation
class RiskEngine final : public IRiskEngine {
public:
    explicit RiskEngine(ILogger* logger);

    // ── IRiskEngine Implementation ──────────────────────────────────────
    RiskScore processDetection(
        const std::string& playerKey,
        const DetectionEvent& event
    ) override;

    [[nodiscard]] RiskScore getPlayerRisk(
        const std::string& playerKey
    ) const override;

    [[nodiscard]] std::vector<RiskScore> getRiskHistory(
        const std::string& playerKey,
        size_t count = 50
    ) const override;

    [[nodiscard]] ActionType determineAction(
        const RiskScore& score
    ) const noexcept override;

    void executeAction(
        const std::string& playerKey,
        ActionType action,
        const std::string& reason
    ) override;

    void resetPlayer(const std::string& playerKey) override;

    [[nodiscard]] std::vector<std::pair<std::string, RiskScore>>
        getAllPlayerRisks() const override;

    void onRiskChanged(RiskCallback callback) override;
    void onDetection(DetectionCallback callback) override;
    void onBan(BanCallback callback) override;

    [[nodiscard]] const RiskActionConfig& actionConfig() const noexcept override;
    void configure(const RiskActionConfig& cfg) override;

    // ── Custom Methods ──────────────────────────────────────────────────

    /// Update risk decay for all players (called periodically)
    void updateDecay(float deltaTime);

    /// Check if a player's score warrants escalation
    void checkEscalation(const std::string& playerKey);

private:
    /// Apply time-based decay to a risk score
    void applyDecay(PlayerRiskState& state, float deltaTime);

    /// Calculate a weighted score giving more weight to recent detections
    [[nodiscard]] uint32_t calculateWeightedScore(
        const PlayerRiskState& state
    ) const;

    /// Get or create risk state for a player
    PlayerRiskState& getOrCreateState(const std::string& playerKey);

    ILogger*                        m_logger;
    RiskActionConfig                m_config;
    RiskThresholds                  m_thresholds;
    std::unordered_map<std::string, PlayerRiskState> m_players;
    mutable std::mutex              m_mutex;

    // Callbacks
    std::vector<RiskCallback>       m_riskCallbacks;
    std::vector<DetectionCallback>  m_detectionCallbacks;
    std::vector<BanCallback>        m_banCallbacks;

    // Decay configuration
    static constexpr float DECAY_RATE = 0.5f;     // Score decay per second
    static constexpr float MAX_DECAY  = 50.0f;     // Max decay over time
    static constexpr size_t MAX_HISTORY = 100;
};

} // namespace sentinel::risk
