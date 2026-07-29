// ============================================================================
// Sentinel AntiCheat - REST API Implementation
// ============================================================================
// Admin REST API providing endpoints for player management, detections,
// logs, statistics, risk assessment, ban management, and system health.
// Uses cpp-httplib for HTTP server functionality.
// ============================================================================

#pragma once
#include "core/IApi.h"
#include "core/IAntiCheatEngine.h"
#include "core/ILogger.h"
#include <string>
#include <memory>
#include <thread>
#include <atomic>

// Forward declare httplib
namespace httplib { class Server; }

namespace sentinel::api {

/// REST API Implementation
class RestApi final : public IApi {
public:
    explicit RestApi(IAntiCheatEngine* engine, ILogger* logger);
    ~RestApi() override;

    // ── IApi Implementation ─────────────────────────────────────────────
    bool start(const ApiConfig& config) override;
    void stop() override;
    [[nodiscard]] bool isRunning() const noexcept override;

    void registerEndpoint(
        const std::string& method,
        const std::string& path,
        std::function<std::string(const std::string& body,
                                   const std::string& query)> handler
    ) override;

    [[nodiscard]] const ApiConfig& config() const noexcept override;
    void configure(const ApiConfig& cfg) override;

    void broadcastEvent(const std::string& eventType,
                         const std::string& jsonData) override;

private:
    // ── Route Handlers ──────────────────────────────────────────────────

    void setupRoutes();

    // GET endpoints
    std::string handleGetPlayers(const std::string& query);
    std::string handleGetPlayer(const std::string& playerKey);
    std::string handleGetDetections(const std::string& query);
    std::string handleGetLogs(const std::string& query);
    std::string handleGetStatistics();
    std::string handleGetRiskLeaderboard(const std::string& query);
    std::string handleGetBans(const std::string& query);
    std::string handleGetHealth();
    std::string handleGetDashboard();

    // POST endpoints
    std::string handlePostAction(const std::string& body);
    std::string handlePostKick(const std::string& body);
    std::string handlePostBan(const std::string& body);
    std::string handlePostUnban(const std::string& body);
    std::string handlePostReloadConfig();

    // ── Helpers ─────────────────────────────────────────────────────────

    /// Create a JSON response
    std::string jsonResponse(bool success, const std::string& data);
    std::string jsonError(const std::string& message, int code = 400);

    /// Validate authentication token
    bool validateAuth(const std::string& authHeader) const;

    /// Parse query parameters from a query string
    std::unordered_map<std::string, std::string> parseQuery(
        const std::string& query
    ) const;

    /// URL decode a string
    std::string urlDecode(const std::string& str) const;

    IAntiCheatEngine*               m_engine;
    ILogger*                        m_logger;
    ApiConfig                       m_config;

    std::unique_ptr<httplib::Server> m_server;
    std::thread                     m_serverThread;
    std::atomic<bool>               m_running{false};

    // Custom endpoint handlers
    std::unordered_map<std::string,
        std::function<std::string(const std::string&, const std::string&)>
    > m_customEndpoints;
};

} // namespace sentinel::api
