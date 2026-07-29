// ============================================================================
// Sentinel AntiCheat - YAML/JSON Configuration Manager
// ============================================================================
// Central configuration manager supporting both YAML and JSON backends.
// All detector thresholds, risk values, and system settings are configurable.
// Supports runtime hot-reload and validation.
// ============================================================================

#pragma once
#include "core/IConfig.h"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <filesystem>

namespace sentinel::config {

/// Configuration Manager Implementation
class YamlConfig final : public IConfig {
public:
    explicit YamlConfig();
    ~YamlConfig() override;

    // ── IConfig Implementation ───────────────────────────────────────────
    bool load(const ConfigSource& source) override;
    bool reload() override;
    bool save() override;

    [[nodiscard]] std::optional<ConfigValue> get(
        const std::string& key
    ) const override;

    void set(
        const std::string& key,
        const ConfigValue& value
    ) override;

    [[nodiscard]] bool has(const std::string& key) const override;

    [[nodiscard]] std::vector<std::string> getSection(
        const std::string& prefix
    ) const override;

    [[nodiscard]] const std::unordered_map<std::string, ConfigEntry>&
        entries() const noexcept override;

    void onChange(std::function<void(const std::string& key)> callback) override;

    [[nodiscard]] const ConfigSource& source() const noexcept override;

    [[nodiscard]] std::vector<std::string> validate() const override;

    void resetToDefaults() override;

private:
    /// Parse a YAML file and populate the config map
    bool parseYamlFile(const std::string& path);

    /// Parse a JSON file and populate the config map
    bool parseJsonFile(const std::string& path);

    /// Flatten nested keys (e.g., {"detectors": {"aim": {"enabled": true}}}
    /// becomes "detectors.aim.enabled" = true)
    void flattenKeys(
        const std::string& prefix,
        const std::unordered_map<std::string, ConfigValue>& nested,
        std::unordered_map<std::string, ConfigEntry>& out
    );

    /// Build default configuration
    void buildDefaults();

    /// Notify all change listeners
    void notifyListeners(const std::string& key);

    ConfigSource                                        m_source;
    std::unordered_map<std::string, ConfigEntry>        m_entries;
    std::vector<std::function<void(const std::string&)>> m_changeCallbacks;
    mutable std::shared_mutex                           m_mutex;
};

} // namespace sentinel::config
