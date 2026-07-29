// ============================================================================
// Sentinel AntiCheat - Configuration Interface
// ============================================================================
// Central configuration manager supporting YAML and JSON backends.
// Every detector, threshold, risk value, and system setting is configurable
// through this interface. Supports runtime hot-reload.
// ============================================================================

#pragma once
#include "Types.h"
#include <string>
#include <unordered_map>
#include <optional>
#include <functional>

namespace sentinel {

/// Configuration file format
enum class ConfigFormat : uint8_t {
    YAML = 0,
    JSON = 1
};

/// Configuration source
struct ConfigSource {
    ConfigFormat format     = ConfigFormat::YAML;
    std::string  path       = "config/sentinel.yaml";
    bool         autoReload = true;
    uint32_t     reloadIntervalSec = 30;
};

/// Configuration entry
struct ConfigEntry {
    std::string  key;
    ConfigValue  value;
    std::string  description;
    bool         reloadable = true;
};

/// Configuration Interface
class IConfig {
public:
    virtual ~IConfig() = default;

    /// Load configuration from source
    virtual bool load(const ConfigSource& source) = 0;

    /// Reload configuration from disk
    virtual bool reload() = 0;

    /// Save current configuration to disk
    virtual bool save() = 0;

    /// Get a configuration value
    [[nodiscard]] virtual std::optional<ConfigValue> get(
        const std::string& key
    ) const = 0;

    /// Get a value with default fallback
    template<typename T>
    [[nodiscard]] T getOrDefault(
        const std::string& key,
        const T& defaultValue
    ) const {
        auto val = get(key);
        if (!val) return defaultValue;
        try {
            return std::get<T>(*val);
        } catch (...) {
            return defaultValue;
        }
    }

    /// Set a configuration value
    virtual void set(
        const std::string& key,
        const ConfigValue& value
    ) = 0;

    /// Check if a key exists
    [[nodiscard]] virtual bool has(const std::string& key) const = 0;

    /// Get all keys in a section (e.g., "detectors.aim")
    [[nodiscard]] virtual std::vector<std::string> getSection(
        const std::string& prefix
    ) const = 0;

    /// Get all configuration entries
    [[nodiscard]] virtual const std::unordered_map<std::string, ConfigEntry>&
        entries() const noexcept = 0;

    /// Register a callback for config changes
    virtual void onChange(std::function<void(const std::string& key)> callback) = 0;

    /// Get the current source
    [[nodiscard]] virtual const ConfigSource& source() const noexcept = 0;

    /// Validate the configuration (check required keys)
    [[nodiscard]] virtual std::vector<std::string> validate() const = 0;

    /// Reset to defaults
    virtual void resetToDefaults() = 0;

    /// Create a detector-specific config section
    [[nodiscard]] DetectorConfig getDetectorConfig(
        const std::string& detectorName
    ) const {
        DetectorConfig cfg;
        cfg.name = detectorName;
        cfg.enabled       = getOrDefault(detectorName + ".enabled", true);
        cfg.riskScore     = getOrDefault(detectorName + ".risk_score", 25u);
        cfg.threshold     = getOrDefault(detectorName + ".threshold", 0.5);
        cfg.minSamples    = getOrDefault(detectorName + ".min_samples", 10u);
        cfg.cooldownMs    = getOrDefault(detectorName + ".cooldown_ms", 5000u);
        return cfg;
    }
};

} // namespace sentinel
