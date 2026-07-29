// ============================================================================
// Sentinel AntiCheat - JSON Logger Implementation
// ============================================================================
// Structured JSON logger with daily log rotation, multiple output targets,
// and admin-readable report generation.
// ============================================================================

#pragma once
#include "core/ILogger.h"
#include <fstream>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>

namespace sentinel::logging {

/// JSON Logger Implementation
class JsonLogger final : public ILogger {
public:
    JsonLogger();
    ~JsonLogger() override;

    // ── ILogger Implementation ───────────────────────────────────────────
    bool initialize(const LoggerConfig& config) override;
    void shutdown() override;

    void log(
        LogLevel level,
        const std::string& module,
        const std::string& message,
        const std::string& playerKey = "",
        std::source_location location = std::source_location::current()
    ) override;

    void logJson(
        LogLevel level,
        const std::string& module,
        const std::string& message,
        const std::string& jsonData,
        const std::string& playerKey = "",
        std::source_location location = std::source_location::current()
    ) override;

    void logDetection(const DetectionEvent& event) override;
    void logBan(const BanRecord& ban) override;

    [[nodiscard]] std::string generateReport(
        const std::string& playerKey = ""
    ) override;

    [[nodiscard]] std::vector<LogEntry> getRecent(
        LogLevel minLevel = LogLevel::Debug,
        size_t count = 100
    ) override;

    [[nodiscard]] std::vector<LogEntry> getPlayerLogs(
        const std::string& playerKey,
        size_t count = 50
    ) override;

    void rotate() override;
    void flush() override;
    void onLog(std::function<void(const LogEntry&)> callback) override;

private:
    /// Write a log entry to file
    void writeToFile(const LogEntry& entry);

    /// Write a log entry to console
    void writeToConsole(const LogEntry& entry);

    /// Format a log entry as JSON
    [[nodiscard]] std::string formatJson(const LogEntry& entry) const;

    /// Format a log entry as human-readable text
    [[nodiscard]] std::string formatHuman(const LogEntry& entry) const;

    /// Get the current log filename based on date
    [[nodiscard]] std::string getCurrentFilename() const;

    /// Check if log rotation is needed
    bool needsRotation() const;

    /// Background flush thread function
    void flushThreadFunc();

    // Configuration
    LoggerConfig        m_config;
    std::string         m_currentFilename;
    std::ofstream       m_fileStream;
    uint32_t            m_currentFileSize = 0;

    // Log buffer
    std::deque<LogEntry> m_buffer;
    mutable std::mutex   m_mutex;

    // Flush thread
    std::thread         m_flushThread;
    std::atomic<bool>   m_running{false};
    std::mutex          m_flushMutex;
    std::condition_variable m_flushCv;

    // Callbacks
    std::vector<std::function<void(const LogEntry&)>> m_callbacks;

    // Log level names
    static constexpr const char* LEVEL_NAMES[] = {
        "DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL",
        "DETECTION", "ADMIN", "BAN"
    };

    // Date helper
    static std::string getDateString();
    static std::string getTimestamp();
};

} // namespace sentinel::logging
