// ============================================================================
// Sentinel AntiCheat - Risk Engine Interface
// ============================================================================
// The Risk Engine is the brain of the anti-cheat system. It:
//   - Aggregates scores from all detectors
//   - Calculates overall risk levels
//   - Determines what actions to take based on accumulated evidence
//   - Maintains risk history per player
// ============================================================================

#pragma once
#include "Types.h"
#include <string>
#include <functional>
#include <vector>

namespace sentinel {

/// Configuration for risk thresholds and actions
struct RiskActionConfig {
    uint32_t    logThreshold          = 15;
    uint32_t    notifyAdminThreshold  = 40;
    uint32_t    recordDemoThreshold   = 60;
    uint32_t    kickThreshold         = 80;
    uint32_t    tempBanThreshold      = 100;
    uint32_t    permBanThreshold      = 150;
    uint32_t    requiredDetections    = 3;    // Min detections before action
    uint32_t    timeWindowSeconds     = 300;  // Detection window for accumulation
    bool        autoKick              = true;
    bool        autoTempBan           = true;
    bool        autoPermBan           = false;
    uint32_t    tempBanDurationMins   = 60;
};

/// Risk level thresholds
struct RiskThresholds {
    uint32_t    safeLevel            = 39;
    uint32_t    suspiciousLevel      = 69;
    uint32_t    highlySuspiciousLevel = 99;
    uint32_t    cheatingLevel        = 149;
    // 150+ = banned
};

/// Risk Engine Interface
class IRiskEngine {
public:
    virtual ~IRiskEngine() = default;

    /// Process a detection result and update player risk
    /// @param playerKey Unique player identifier
    /// @param result The detection event
    /// @return Updated risk score for the player
    virtual RiskScore processDetection(
        const std::string& playerKey,
        const DetectionEvent& event
    ) = 0;

    /// Get current risk score for a player
    [[nodiscard]] virtual RiskScore getPlayerRisk(
        const std::string& playerKey
    ) const = 0;

    /// Get risk history for a player
    [[nodiscard]] virtual std::vector<RiskScore> getRiskHistory(
        const std::string& playerKey,
        size_t count = 50
    ) const = 0;

    /// Determine what action to take based on current risk
    [[nodiscard]] virtual ActionType determineAction(
        const RiskScore& score
    ) const noexcept = 0;

    /// Execute an action against a player
    virtual void executeAction(
        const std::string& playerKey,
        ActionType action,
        const std::string& reason
    ) = 0;

    /// Reset risk state for a player
    virtual void resetPlayer(const std::string& playerKey) = 0;

    /// Get all current player risks
    [[nodiscard]] virtual std::vector<std::pair<std::string, RiskScore>>
        getAllPlayerRisks() const = 0;

    /// Register callback for risk level changes
    virtual void onRiskChanged(RiskCallback callback) = 0;

    /// Register callback for detection events
    virtual void onDetection(DetectionCallback callback) = 0;

    /// Register callback for ban events
    virtual void onBan(BanCallback callback) = 0;

    /// Get configuration
    [[nodiscard]] virtual const RiskActionConfig& actionConfig() const noexcept = 0;

    /// Update configuration at runtime
    virtual void configure(const RiskActionConfig& cfg) = 0;
};

} // namespace sentinel
