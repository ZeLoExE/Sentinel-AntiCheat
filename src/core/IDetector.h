// ============================================================================
// Sentinel AntiCheat - Detector Interface
// ============================================================================
// All detection modules must implement this interface.
// Each detector analyzes a specific type of cheating behavior and returns
// a suspicion score that feeds into the Risk Engine.
// ============================================================================

#pragma once
#include "Types.h"
#include <string>
#include <memory>
#include <vector>

namespace sentinel {

// ── Detector Configuration ───────────────────────────────────────────────
struct DetectorConfig {
    bool        enabled          = true;
    uint32_t    riskScore        = 25;    // Score added when detected
    float       threshold        = 0.5f;  // Detection sensitivity (0-1)
    uint32_t    minSamples       = 10;    // Min data points before analysis
    uint32_t    cooldownMs       = 5000;  // Min ms between detections
    std::string name;
    std::string description;
};

// ── Detection Result ─────────────────────────────────────────────────────
struct DetectionResult {
    bool        detected         = false;
    float       confidence       = 0.0f;  // 0.0 - 1.0
    uint32_t    score            = 0;
    std::string evidence;                  // Human-readable evidence string
    std::string moduleName;
    DetectionType type;
};

// ── Detector Interface ───────────────────────────────────────────────────
///
/// Abstract base class for all cheat detection modules.
/// Every detector:
///   1. Analyzes player data for a specific cheat type
///   2. Returns a DetectionResult with confidence and score
///   3. Is independently configurable and enable/disable-able
///
class IDetector {
public:
    virtual ~IDetector() = default;

    /// Unique identifier for this detector
    [[nodiscard]] virtual const std::string& name() const noexcept = 0;

    /// Human-readable description of what this detector checks
    [[nodiscard]] virtual const std::string& description() const noexcept = 0;

    /// The type of detection this module performs
    [[nodiscard]] virtual DetectionType detectionType() const noexcept = 0;

    /// Run detection analysis on the given player key.
    /// @param playerKey Unique identifier for the player
    /// @param deltaTime Seconds since last update
    /// @return DetectionResult with findings (may be negative/empty)
    [[nodiscard]] virtual DetectionResult detect(
        const std::string& playerKey,
        float deltaTime
    ) = 0;

    /// Reset detector state for a player (e.g., on disconnect)
    virtual void reset(const std::string& playerKey) = 0;

    /// Reset all state (e.g., on map change)
    virtual void resetAll() = 0;

    /// Get current configuration
    [[nodiscard]] virtual const DetectorConfig& config() const noexcept = 0;

    /// Update configuration at runtime
    virtual void configure(const DetectorConfig& cfg) = 0;

    /// Whether this detector needs per-frame updates
    [[nodiscard]] virtual bool requiresFrameUpdate() const noexcept = 0;

    /// Per-frame update called every server frame
    virtual void onFrameUpdate(float deltaTime) = 0;

    /// Get the detector's current suspicion score for a player (0-100)
    [[nodiscard]] virtual float currentSuspicion(
        const std::string& playerKey
    ) const noexcept = 0;
};

// ── Utility ──────────────────────────────────────────────────────────────
using DetectorPtr = std::unique_ptr<IDetector>;

} // namespace sentinel
