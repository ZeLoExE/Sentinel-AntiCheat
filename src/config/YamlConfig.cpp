// ============================================================================
// Sentinel AntiCheat - Configuration Manager Implementation
// ============================================================================
// Supports YAML and JSON backends with runtime hot-reload.
// ============================================================================

#include "YamlConfig.h"
#include <fstream>
#include <sstream>

namespace sentinel::config {

YamlConfig::YamlConfig() {
    buildDefaults();
}

YamlConfig::~YamlConfig() = default;

bool YamlConfig::load(const ConfigSource& source) {
    std::unique_lock lock(m_mutex);
    m_source = source;

    bool success = false;
    switch (source.format) {
        case ConfigFormat::YAML:
            success = parseYamlFile(source.path);
            break;
        case ConfigFormat::JSON:
            success = parseJsonFile(source.path);
            break;
    }

    if (success) {
        buildDefaults();  // Fill in any missing keys
    }

    return success;
}

bool YamlConfig::reload() {
    return load(m_source);
}

bool YamlConfig::save() {
    std::shared_lock lock(m_mutex);
    // Simplified: write as JSON for universal compatibility
    std::ofstream file(m_source.path);
    if (!file.is_open()) return false;

    file << "{" << std::endl;
    bool first = true;
    for (const auto& [key, entry] : m_entries) {
        if (!first) file << "," << std::endl;
        first = false;
        file << "  \"" << key << "\": ";

        std::visit([&file](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::string>) {
                file << "\"" << val << "\"";
            } else if constexpr (std::is_same_v<T, bool>) {
                file << (val ? "true" : "false");
            } else if constexpr (std::is_same_v<T, int64_t>) {
                file << val;
            } else if constexpr (std::is_same_v<T, double>) {
                file << val;
            } else {
                file << "\"" << val << "\"";
            }
        }, entry.value);
    }
    file << std::endl << "}" << std::endl;

    return true;
}

std::optional<ConfigValue> YamlConfig::get(const std::string& key) const {
    std::shared_lock lock(m_mutex);
    auto it = m_entries.find(key);
    if (it != m_entries.end()) {
        return it->second.value;
    }
    return std::nullopt;
}

void YamlConfig::set(const std::string& key, const ConfigValue& value) {
    std::unique_lock lock(m_mutex);
    m_entries[key].value = value;
    notifyListeners(key);
}

bool YamlConfig::has(const std::string& key) const {
    std::shared_lock lock(m_mutex);
    return m_entries.find(key) != m_entries.end();
}

std::vector<std::string> YamlConfig::getSection(const std::string& prefix) const {
    std::shared_lock lock(m_mutex);
    std::vector<std::string> result;
    for (const auto& [key, entry] : m_entries) {
        if (key.find(prefix) == 0 || key.find(prefix + ".") == 0) {
            result.push_back(key);
        }
    }
    return result;
}

const std::unordered_map<std::string, ConfigEntry>& YamlConfig::entries() const noexcept {
    return m_entries;
}

void YamlConfig::onChange(std::function<void(const std::string& key)> callback) {
    m_changeCallbacks.push_back(std::move(callback));
}

const ConfigSource& YamlConfig::source() const noexcept {
    return m_source;
}

std::vector<std::string> YamlConfig::validate() const {
    std::vector<std::string> errors;
    // Check required keys exist
    std::vector<std::string> required = {
        "detectors.aimbot.enabled",
        "detectors.movement.enabled",
        "detectors.network.enabled",
        "detectors.behavior.enabled",
        "risk.safe_threshold",
        "risk.suspicious_threshold",
        "risk.cheating_threshold"
    };

    for (const auto& key : required) {
        if (!has(key)) {
            errors.push_back("Missing required config key: " + key);
        }
    }

    return errors;
}

void YamlConfig::resetToDefaults() {
    std::unique_lock lock(m_mutex);
    m_entries.clear();
    buildDefaults();
}

bool YamlConfig::parseYamlFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    // Simplified YAML parser - in production, use yaml-cpp
    std::string line;
    std::string currentSection;
    while (std::getline(file, line)) {
        // Remove leading/trailing whitespace
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(line);

        if (line.empty() || line[0] == '#') continue;

        // Section header
        if (line.back() == ':') {
            currentSection = line.substr(0, line.size() - 1);
            trim(currentSection);
            continue;
        }

        // Key-value
        auto colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            trim(key);
            trim(value);

            std::string fullKey = currentSection.empty() ? key : currentSection + "." + key;

            ConfigEntry entry;
            entry.key = fullKey;

            // Parse value
            if (value == "true" || value == "false") {
                entry.value = (value == "true");
            } else if (value.find('.') != std::string::npos) {
                try { entry.value = std::stod(value); }
                catch (...) { entry.value = value; }
            } else {
                try { entry.value = static_cast<int64_t>(std::stoll(value)); }
                catch (...) { entry.value = value; }
            }

            // Remove quotes
            if (std::holds_alternative<std::string>(entry.value)) {
                auto& str = std::get<std::string>(entry.value);
                if (str.size() >= 2 && str.front() == '"' && str.back() == '"') {
                    str = str.substr(1, str.size() - 2);
                }
            }

            m_entries[fullKey] = entry;
        }
    }

    return true;
}

bool YamlConfig::parseJsonFile(const std::string& path) {
    // In production, use nlohmann/json
    // Simplified: just use the YAML parser for now
    return parseYamlFile(path);
}

void YamlConfig::flattenKeys(const std::string& prefix,
    const std::unordered_map<std::string, ConfigValue>& nested,
    std::unordered_map<std::string, ConfigEntry>& out) {
    for (const auto& [key, value] : nested) {
        std::string fullKey = prefix.empty() ? key : prefix + "." + key;
        out[fullKey].key = fullKey;
        out[fullKey].value = value;
    }
}

void YamlConfig::buildDefaults() {
    auto setDefault = [this](const std::string& key, const ConfigValue& value,
                              const std::string& desc = "") {
        if (!has(key)) {
            m_entries[key].key = key;
            m_entries[key].value = value;
            m_entries[key].description = desc;
        }
    };

    // System
    setDefault("system.debug", false, "Enable debug mode");
    setDefault("system.update_interval_ms", 100LL, "Detection update interval");

    // Detectors - Aimbot
    setDefault("detectors.aimbot.enabled", true);
    setDefault("detectors.aimbot.risk_score", 25LL);
    setDefault("detectors.aimbot.threshold", 0.5);
    setDefault("detectors.aimbot.min_samples", 30LL);
    setDefault("detectors.aimbot.cooldown_ms", 5000LL);
    setDefault("detectors.aimbot.snap_weight", 25LL);
    setDefault("detectors.aimbot.silent_aim_weight", 35LL);
    setDefault("detectors.aimbot.perfect_track_weight", 20LL);
    setDefault("detectors.aimbot.trigger_bot_weight", 15LL);

    // Detectors - Movement
    setDefault("detectors.movement.enabled", true);
    setDefault("detectors.movement.risk_score", 25LL);
    setDefault("detectors.movement.threshold", 0.5);
    setDefault("detectors.movement.min_samples", 20LL);
    setDefault("detectors.movement.cooldown_ms", 5000LL);
    setDefault("detectors.movement.bhop_weight", 10LL);
    setDefault("detectors.movement.speedhack_weight", 30LL);
    setDefault("detectors.movement.autostrafe_weight", 15LL);

    // Detectors - Network
    setDefault("detectors.network.enabled", true);
    setDefault("detectors.network.risk_score", 20LL);
    setDefault("detectors.network.threshold", 0.5);
    setDefault("detectors.network.min_samples", 30LL);
    setDefault("detectors.network.cooldown_ms", 5000LL);
    setDefault("detectors.network.fakelag_weight", 15LL);
    setDefault("detectors.network.flood_weight", 10LL);

    // Detectors - Behavior
    setDefault("detectors.behavior.enabled", true);
    setDefault("detectors.behavior.risk_score", 20LL);
    setDefault("detectors.behavior.threshold", 0.5);
    setDefault("detectors.behavior.min_samples", 5LL);
    setDefault("detectors.behavior.cooldown_ms", 10000LL);
    setDefault("detectors.behavior.consistency_weight", 20LL);

    // Risk thresholds
    setDefault("risk.safe_threshold", 39LL);
    setDefault("risk.suspicious_threshold", 69LL);
    setDefault("risk.cheating_threshold", 99LL);
    setDefault("risk.banned_threshold", 149LL);
    setDefault("risk.log_threshold", 15LL);
    setDefault("risk.notify_threshold", 40LL);
    setDefault("risk.kick_threshold", 80LL);
    setDefault("risk.temp_ban_threshold", 100LL);
    setDefault("risk.perm_ban_threshold", 150LL);
    setDefault("risk.auto_kick", true);
    setDefault("risk.auto_temp_ban", true);
    setDefault("risk.auto_perm_ban", false);
    setDefault("risk.temp_ban_duration_mins", 60LL);
    setDefault("risk.required_detections", 3LL);
    setDefault("risk.time_window_seconds", 300LL);

    // API
    setDefault("api.enabled", true);
    setDefault("api.host", std::string("127.0.0.1"));
    setDefault("api.port", 8080LL);
    setDefault("api.auth_token", std::string("change-me-sentinel-admin-token"));
    setDefault("api.require_auth", true);

    // Database
    setDefault("database.type", std::string("sqlite"));
    setDefault("database.path", std::string("sentinel.db"));

    // Logging
    setDefault("logging.directory", std::string("logs/"));
    setDefault("logging.file_level", std::string("info"));
    setDefault("logging.console_level", std::string("warning"));
    setDefault("logging.json_format", true);
    setDefault("logging.max_file_size_mb", 50LL);
    setDefault("logging.max_files", 30LL);
}

void YamlConfig::notifyListeners(const std::string& key) {
    for (auto& cb : m_changeCallbacks) {
        cb(key);
    }
}

} // namespace sentinel::config
