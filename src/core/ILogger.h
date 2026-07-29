// ============================================================================
// Sentinel AntiCheat - Logger Interface
// ============================================================================
// Structured JSON logging with daily rotation, multiple log levels,
// and admin-readable report generation.
// ============================================================================

#pragma once
#include "Types.h"
#include <string>
#include <string_view>
#include <source_location>
#include <functional>

namespace sentinel {

/// Log severity levels
enum class LogLevel : uint8_t {
    Debug     = 0,
    Info      = 1,
    Warning   = 2,
    Error     = 3,
    Critical  = 4,
    Detection = 5,  // Cheat detection specific
    Admin     = 6,  // Admin notification level
    Ban       = 7   // Ban event
};

/// Logger output destination flags
enum class LogOutput : uint8_t {
    Console   = 0x01,
    File      = 0x02,
    Both      = 0x03,
    Network   = 0x04   // Remote logging via API
};

/// Log entry structure
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel        level       = LogLevel::Info;
    std::string     module;
    std::string     message;
    std::string     playerKey;
    std::string     sessionId;
    std::string     file;
    uint32_t        line        = 0;
    std::string     function;
    std::string     jsonExtra;  // Additional JSON data
};

/// Logger configuration
struct LoggerConfig {
    std::string     logDirectory    = "logs/";
    LogLevel        fileLevel       = LogLevel::Info;
    LogLevel        consoleLevel    = LogLevel::Warning;
    LogOutput       output          = LogOutput::Both;
    uint32_t        maxFileSizeMB   = 50;
    uint32_t        maxFiles        = 30;    // Keep 30 days of logs
    bool            jsonFormat      = true;
    bool            includeTimestamp = true;
    bool            debugMode       = false;
    bool            logDetections   = true;
    bool            logPlayerActivity = true;
    bool            logAdminActions = true;
};

/// Logger Interface
class ILogger {
public:
    virtual ~ILogger() = default;

    /// Initialize the logger
    virtual bool initialize(const LoggerConfig& config) = 0;

    /// Shutdown the logger
    virtual void shutdown() = 0;

    /// Log a message
    virtual void log(
        LogLevel level,
        const std::string& module,
        const std::string& message,
        const std::string& playerKey = "",
        std::source_location location = std::source_location::current()
    ) = 0;

    /// Log with extra JSON data
    virtual void logJson(
        LogLevel level,
        const std::string& module,
        const std::string& message,
        const std::string& jsonData,
        const std::string& playerKey = "",
        std::source_location location = std::source_location::current()
    ) = 0;

    /// Log a cheat detection event
    virtual void logDetection(
        const DetectionEvent& event
    ) = 0;

    /// Log a ban event
    virtual void logBan(
        const BanRecord& ban
    ) = 0;

    /// Generate an admin-readable report
    [[nodiscard]] virtual std::string generateReport(
        const std::string& playerKey = ""
    ) = 0;

    /// Get recent log entries
    [[nodiscard]] virtual std::vector<LogEntry> getRecent(
        LogLevel minLevel = LogLevel::Debug,
        size_t count = 100
    ) = 0;

    /// Get log entries for a specific player
    [[nodiscard]] virtual std::vector<LogEntry> getPlayerLogs(
        const std::string& playerKey,
        size_t count = 50
    ) = 0;

    /// Rotate log files manually
    virtual void rotate() = 0;

    /// Flush logs to disk
    virtual void flush() = 0;

    /// Register a log callback (for real-time log streaming)
    virtual void onLog(std::function<void(const LogEntry&)> callback) = 0;

    /// Convenience methods
    void debug(const std::string& module, const std::string& msg,
               std::source_location loc = std::source_location::current()) {
        log(LogLevel::Debug, module, msg, "", loc);
    }
    void info(const std::string& module, const std::string& msg,
              std::source_location loc = std::source_location::current()) {
        log(LogLevel::Info, module, msg, "", loc);
    }
    void warn(const std::string& module, const std::string& msg,
              std::source_location loc = std::source_location::current()) {
        log(LogLevel::Warning, module, msg, "", loc);
    }
    void error(const std::string& module, const std::string& msg,
               std::source_location loc = std::source_location::current()) {
        log(LogLevel::Error, module, msg, "", loc);
    }
    void critical(const std::string& module, const std::string& msg,
                  std::source_location loc = std::source_location::current()) {
        log(LogLevel::Critical, module, msg, "", loc);
    }
};

} // namespace sentinel
