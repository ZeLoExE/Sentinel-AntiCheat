// ============================================================================
// Sentinel AntiCheat - JSON Logger Implementation
// ============================================================================
// Structured JSON logging with daily rotation, thread-safe buffered writes,
// and multiple output targets.
// ============================================================================

#include "JsonLogger.h"
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ctime>

namespace sentinel::logging {

JsonLogger::JsonLogger() = default;

JsonLogger::~JsonLogger() {
    shutdown();
}

bool JsonLogger::initialize(const LoggerConfig& config) {
    m_config = config;
    m_currentFilename = getCurrentFilename();

    if (static_cast<uint8_t>(config.output) & static_cast<uint8_t>(LogOutput::File)) {
        // Ensure directory exists
        std::filesystem::path logDir(config.logDirectory);
        if (!std::filesystem::exists(logDir)) {
            std::filesystem::create_directories(logDir);
        }

        m_fileStream.open(m_currentFilename, std::ios::app);
        if (!m_fileStream.is_open()) {
            return false;
        }

        // Start background flush thread
        m_running = true;
        m_flushThread = std::thread(&JsonLogger::flushThreadFunc, this);
    }

    return true;
}

void JsonLogger::shutdown() {
    m_running = false;
    if (m_flushCv) m_flushCv.notify_one();
    if (m_flushThread.joinable()) m_flushThread.join();

    flush();

    if (m_fileStream.is_open()) {
        m_fileStream.close();
    }
}

void JsonLogger::log(LogLevel level, const std::string& module,
                      const std::string& message, const std::string& playerKey,
                      std::source_location location) {
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = level;
    entry.module = module;
    entry.message = message;
    entry.playerKey = playerKey;
    entry.file = location.file_name();
    entry.line = location.line();
    entry.function = location.function_name();

    // Write to console immediately
    if (static_cast<uint8_t>(m_config.output) & static_cast<uint8_t>(LogOutput::Console)) {
        if (level >= m_config.consoleLevel) {
            writeToConsole(entry);
        }
    }

    // Buffer for file output
    if (static_cast<uint8_t>(m_config.output) & static_cast<uint8_t>(LogOutput::File)) {
        if (level >= m_config.fileLevel) {
            std::lock_guard lock(m_mutex);
            m_buffer.push_back(entry);
            if (m_buffer.size() >= 100) {
                m_flushCv.notify_one();
            }
            if (needsRotation()) {
                rotate();
            }
        }
    }

    // Notify callbacks
    for (auto& cb : m_callbacks) {
        cb(entry);
    }
}

void JsonLogger::logJson(LogLevel level, const std::string& module,
                          const std::string& message, const std::string& jsonData,
                          const std::string& playerKey, std::source_location location) {
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = level;
    entry.module = module;
    entry.message = message;
    entry.playerKey = playerKey;
    entry.jsonExtra = jsonData;
    log(level, module, message, playerKey, location);
}

void JsonLogger::logDetection(const DetectionEvent& event) {
    std::string json = R"({"type":")" + std::to_string(static_cast<int>(event.type)) +
        R"(","score":)" + std::to_string(event.score) +
        R"(,"confidence":)" + std::to_string(event.confidence) +
        R"(,"evidence":")" + event.description + R"("})";

    LogEntry entry;
    entry.timestamp = event.timestamp;
    entry.level = LogLevel::Detection;
    entry.module = event.moduleName;
    entry.message = "Detection: " + event.description;
    entry.playerKey = event.playerKey;
    entry.jsonExtra = json;

    if (static_cast<uint8_t>(m_config.output) & static_cast<uint8_t>(LogOutput::Console)) {
        writeToConsole(entry);
    }

    if (static_cast<uint8_t>(m_config.output) & static_cast<uint8_t>(LogOutput::File)) {
        std::lock_guard lock(m_mutex);
        m_buffer.push_back(entry);
    }

    for (auto& cb : m_callbacks) cb(entry);
}

void JsonLogger::logBan(const BanRecord& ban) {
    LogEntry entry;
    entry.timestamp = ban.issuedAt;
    entry.level = LogLevel::Ban;
    entry.module = "RiskEngine";
    entry.message = "Ban: " + ban.playerKey + " - " + ban.reason;
    entry.playerKey = ban.playerKey;

    if (static_cast<uint8_t>(m_config.output) & static_cast<uint8_t>(LogOutput::Console)) {
        writeToConsole(entry);
    }

    if (static_cast<uint8_t>(m_config.output) & static_cast<uint8_t>(LogOutput::File)) {
        std::lock_guard lock(m_mutex);
        m_buffer.push_back(entry);
    }

    for (auto& cb : m_callbacks) cb(entry);
}

std::string JsonLogger::generateReport(const std::string& playerKey) {
    std::lock_guard lock(m_mutex);
    std::string report = "=== Sentinel AntiCheat Report ===\n";

    if (!playerKey.empty()) {
        report += "Player: " + playerKey + "\n";
        auto logs = getPlayerLogs(playerKey, 50);
        for (const auto& log : logs) {
            report += formatHuman(log) + "\n";
        }
    } else {
        report += "Recent Activities:\n";
        auto logs = getRecent(LogLevel::Info, 20);
        for (const auto& log : logs) {
            report += formatHuman(log) + "\n";
        }
    }

    return report;
}

std::vector<LogEntry> JsonLogger::getRecent(LogLevel minLevel, size_t count) {
    std::lock_guard lock(m_mutex);
    std::vector<LogEntry> result;
    for (const auto& entry : m_buffer) {
        if (entry.level >= minLevel) {
            result.push_back(entry);
            if (result.size() >= count) break;
        }
    }
    return result;
}

std::vector<LogEntry> JsonLogger::getPlayerLogs(const std::string& playerKey, size_t count) {
    std::lock_guard lock(m_mutex);
    std::vector<LogEntry> result;
    for (const auto& entry : m_buffer) {
        if (entry.playerKey == playerKey) {
            result.push_back(entry);
            if (result.size() >= count) break;
        }
    }
    return result;
}

void JsonLogger::rotate() {
    std::lock_guard lock(m_mutex);

    if (m_fileStream.is_open()) {
        m_fileStream.close();
    }

    // Rename current log file with timestamp
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << m_config.logDirectory << "sentinel_"
       << std::put_time(std::localtime(&timeT), "%Y%m%d_%H%M%S")
       << ".log";

    try {
        if (std::filesystem::exists(m_currentFilename)) {
            std::filesystem::rename(m_currentFilename, ss.str());
        }
    } catch (...) {}

    // Open new log file
    m_currentFilename = getCurrentFilename();
    m_fileStream.open(m_currentFilename, std::ios::app);
    m_currentFileSize = 0;

    // Clean old logs
    try {
        std::vector<std::filesystem::path> oldLogs;
        for (const auto& entry : std::filesystem::directory_iterator(m_config.logDirectory)) {
            if (entry.path().extension() == ".log" &&
                entry.path().filename().string().find("sentinel_") == 0) {
                oldLogs.push_back(entry.path());
            }
        }

        std::sort(oldLogs.begin(), oldLogs.end());
        while (oldLogs.size() > m_config.maxFiles) {
            std::filesystem::remove(oldLogs.front());
            oldLogs.erase(oldLogs.begin());
        }
    } catch (...) {}
}

void JsonLogger::flush() {
    if (static_cast<uint8_t>(m_config.output) & static_cast<uint8_t>(LogOutput::File)) {
        std::lock_guard lock(m_mutex);
        while (!m_buffer.empty()) {
            writeToFile(m_buffer.front());
            m_buffer.pop_front();
        }
        if (m_fileStream.is_open()) {
            m_fileStream.flush();
        }
    }
}

void JsonLogger::onLog(std::function<void(const LogEntry&)> callback) {
    m_callbacks.push_back(std::move(callback));
}

void JsonLogger::writeToFile(const LogEntry& entry) {
    if (!m_fileStream.is_open()) return;

    std::string formatted;
    if (m_config.jsonFormat) {
        formatted = formatJson(entry);
    } else {
        formatted = formatHuman(entry);
    }

    m_fileStream << formatted << std::endl;
    m_currentFileSize += static_cast<uint32_t>(formatted.size());
}

void JsonLogger::writeToConsole(const LogEntry& entry) {
    std::string formatted = formatHuman(entry);

    switch (entry.level) {
        case LogLevel::Error:
        case LogLevel::Critical:
        case LogLevel::Ban:
            std::cerr << formatted << std::endl;
            break;
        default:
            std::cout << formatted << std::endl;
            break;
    }
}

std::string JsonLogger::formatJson(const LogEntry& entry) const {
    std::stringstream ss;
    ss << R"({"timestamp":")" << getTimestamp()
       << R"(","level":")" << LEVEL_NAMES[static_cast<uint8_t>(entry.level)]
       << R"(","module":")" << entry.module
       << R"(","message":")" << entry.message
       << R"(","player":")" << entry.playerKey;

    if (!entry.jsonExtra.empty()) {
        ss << R"(","extra":)" << entry.jsonExtra;
    }

    ss << R"("})";
    return ss.str();
}

std::string JsonLogger::formatHuman(const LogEntry& entry) const {
    std::stringstream ss;
    ss << "[" << getTimestamp() << "] "
       << std::setw(8) << std::left << LEVEL_NAMES[static_cast<uint8_t>(entry.level)]
       << " [" << entry.module << "] "
       << entry.message;

    if (!entry.playerKey.empty()) {
        ss << " (player: " << entry.playerKey << ")";
    }

    return ss.str();
}

std::string JsonLogger::getCurrentFilename() const {
    return m_config.logDirectory + "sentinel_" + getDateString() + ".log";
}

bool JsonLogger::needsRotation() const {
    return m_currentFileSize >= (m_config.maxFileSizeMB * 1024 * 1024);
}

void JsonLogger::flushThreadFunc() {
    while (m_running) {
        {
            std::unique_lock lock(m_flushMutex);
            m_flushCv.wait_for(lock, std::chrono::seconds(5), [this] {
                return !m_running || m_buffer.size() >= 100;
            });
        }
        flush();
    }
}

std::string JsonLogger::getDateString() {
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&timeT), "%Y%m%d");
    return ss.str();
}

std::string JsonLogger::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    std::stringstream ss;
    ss << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S")
       << "." << std::setfill('0') << std::setw(3) << ms;
    return ss.str();
}

constexpr const char* JsonLogger::LEVEL_NAMES[];

} // namespace sentinel::logging
