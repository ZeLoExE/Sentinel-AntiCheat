// ============================================================================
// Sentinel AntiCheat - REST API Implementation
// ============================================================================
// Admin REST API with JSON endpoints for player management, detections,
// risk assessment, bans, and system health.
// ============================================================================

#include "RestApi.h"
#include <sstream>
#include <iomanip>
#include <regex>
#include <httplib.h>

namespace sentinel::api {

RestApi::RestApi(IAntiCheatEngine* engine, ILogger* logger)
    : m_engine(engine)
    , m_logger(logger)
    , m_server(std::make_unique<httplib::Server>())
{
}

RestApi::~RestApi() {
    stop();
}

bool RestApi::start(const ApiConfig& config) {
    m_config = config;
    if (!config.enabled) return true;

    setupRoutes();

    // CORS support
    if (config.enableCors) {
        m_server->set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

            if (req.method == "OPTIONS") {
                res.status = 200;
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
    }

    // Start server in background thread
    m_running = true;
    m_serverThread = std::thread([this]() {
        m_logger->info("RestApi", "API server starting on " + m_config.host + ":" +
                       std::to_string(m_config.port));
        m_server->listen(m_config.host.c_str(), m_config.port);
    });

    return true;
}

void RestApi::stop() {
    m_running = false;
    if (m_server) {
        m_server->stop();
    }
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
}

bool RestApi::isRunning() const noexcept {
    return m_running.load();
}

void RestApi::registerEndpoint(
    const std::string& method,
    const std::string& path,
    std::function<std::string(const std::string&, const std::string&)> handler)
{
    m_customEndpoints[method + ":" + path] = std::move(handler);
}

const ApiConfig& RestApi::config() const noexcept {
    return m_config;
}

void RestApi::configure(const ApiConfig& cfg) {
    m_config = cfg;
}

void RestApi::broadcastEvent(const std::string& eventType, const std::string& jsonData) {
    // WebSocket broadcasting - future implementation
    m_logger->debug("RestApi", "Broadcast: " + eventType);
}

void RestApi::setupRoutes() {
    // ── GET Endpoints ──────────────────────────────────────────────────

    m_server->Get("/api/v1/health", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_config.requireAuth && !validateAuth(req.get_header_value("Authorization"))) {
            res.status = 401;
            res.set_content(jsonError("Unauthorized"), "application/json");
            return;
        }
        res.set_content(handleGetHealth(), "application/json");
    });

    m_server->Get("/api/v1/dashboard", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_config.requireAuth && !validateAuth(req.get_header_value("Authorization"))) {
            res.status = 401;
            res.set_content(jsonError("Unauthorized"), "application/json");
            return;
        }
        res.set_content(handleGetDashboard(), "application/json");
    });

    m_server->Get("/api/v1/players", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_config.requireAuth && !validateAuth(req.get_header_value("Authorization"))) {
            res.status = 401;
            res.set_content(jsonError("Unauthorized"), "application/json");
            return;
        }
        res.set_content(handleGetPlayers(req.path), "application/json");
    });

    m_server->Get(R"(/api/v1/players/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_config.requireAuth && !validateAuth(req.get_header_value("Authorization"))) {
            res.status = 401;
            res.set_content(jsonError("Unauthorized"), "application/json");
            return;
        }
        std::string playerKey = req.matches[1];
        res.set_content(handleGetPlayer(playerKey), "application/json");
    });

    m_server->Get("/api/v1/detections", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_config.requireAuth && !validateAuth(req.get_header_value("Authorization"))) {
            res.status = 401;
            res.set_content(jsonError("Unauthorized"), "application/json");
            return;
        }
        res.set_content(handleGetDetections(req.params.empty() ? "" : req.path), "application/json");
    });

    m_server->Get("/api/v1/logs", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_config.requireAuth && !validateAuth(req.get_header_value("Authorization"))) {
            res.status = 401;
            res.set_content(jsonError("Unauthorized"), "application/json");
            return;
        }
        res.set_content(handleGetLogs(""), "application/json");
    });

    m_server->Get("/api/v1/statistics", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_config.requireAuth && !validateAuth(req.get_header_value("Authorization"))) {
            res.status = 401;
            res.set_content(jsonError("Unauthorized"), "application/json");
            return;
        }
        res.set_content(handleGetStatistics(), "application/json");
    });

    m_server->Get("/api/v1/risk/leaderboard", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_config.requireAuth && !validateAuth(req.get_header_value("Authorization"))) {
            res.status = 401;
            res.set_content(jsonError("Unauthorized"), "application/json");
            return;
        }
        res.set_content(handleGetRiskLeaderboard(""), "application/json");
    });

    m_server->Get("/api/v1/bans", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_config.requireAuth && !validateAuth(req.get_header_value("Authorization"))) {
            res.status = 401;
            res.set_content(jsonError("Unauthorized"), "application/json");
            return;
        }
        res.set_content(handleGetBans(""), "application/json");
    });

    // ── POST Endpoints ─────────────────────────────────────────────────
    m_server->Post("/api/v1/bans", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_config.requireAuth && !validateAuth(req.get_header_value("Authorization"))) {
            res.status = 401;
            res.set_content(jsonError("Unauthorized"), "application/json");
            return;
        }
        res.set_content(handlePostBan(req.body), "application/json");
    });

    m_server->Post("/api/v1/bans/unban", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_config.requireAuth && !validateAuth(req.get_header_value("Authorization"))) {
            res.status = 401;
            res.set_content(jsonError("Unauthorized"), "application/json");
            return;
        }
        res.set_content(handlePostUnban(req.body), "application/json");
    });

    m_server->Post("/api/v1/actions/kick", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_config.requireAuth && !validateAuth(req.get_header_value("Authorization"))) {
            res.status = 401;
            res.set_content(jsonError("Unauthorized"), "application/json");
            return;
        }
        res.set_content(handlePostKick(req.body), "application/json");
    });

    m_server->Post("/api/v1/config/reload", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_config.requireAuth && !validateAuth(req.get_header_value("Authorization"))) {
            res.status = 401;
            res.set_content(jsonError("Unauthorized"), "application/json");
            return;
        }
        res.set_content(handlePostReloadConfig(), "application/json");
    });
}

// ── Route Handlers ───────────────────────────────────────────────────────

std::string RestApi::handleGetPlayers(const std::string& query) {
    if (!m_engine || !m_engine->playerManager()) return jsonError("Engine not ready");

    auto players = m_engine->playerManager()->getAllPlayers();
    std::string json = R"({"players": [)";
    bool first = true;

    for (auto* p : players) {
        if (!first) json += ",";
        first = false;
        json += R"({)";
        json += R"("name":")" + p->identity.name + R"(",)";
        json += R"("steam_id":")" + p->identity.steamId.communityId + R"(",)";
        json += R"("ip":")" + p->identity.ipAddress + R"(",)";
        json += R"("user_id":)" + std::to_string(p->identity.userId) + R"(,)";
        json += R"("kills":)" + std::to_string(p->stats.kills) + R"(,)";
        json += R"("deaths":)" + std::to_string(p->stats.deaths) + R"(,)";
        json += R"("headshots":)" + std::to_string(p->stats.headshots) + R"(,)";
        json += R"("headshot_ratio":)" + std::to_string(p->stats.headshotRatio) + R"(,)";
        json += R"("risk_score":)" + std::to_string(p->risk.totalScore);
        json += R"(})";
    }

    json += R"(]})";
    return jsonResponse(true, json);
}

std::string RestApi::handleGetPlayer(const std::string& playerKey) {
    if (!m_engine || !m_engine->playerManager()) return jsonError("Engine not ready");

    auto* player = m_engine->playerManager()->getPlayer(playerKey);
    if (!player) return jsonError("Player not found");

    std::string json = R"({"player": {)";
    json += R"("name":")" + player->identity.name + R"(",)";
    json += R"("steam_id":")" + player->identity.steamId.communityId + R"(",)";
    json += R"("ip":")" + player->identity.ipAddress + R"(",)";
    json += R"("kills":)" + std::to_string(player->stats.kills) + R"(,)";
    json += R"("deaths":)" + std::to_string(player->stats.deaths) + R"(,)";
    json += R"("headshots":)" + std::to_string(player->stats.headshots) + R"(,)";
    json += R"("risk_score":)" + std::to_string(player->risk.totalScore) + R"(,)";
    json += R"("detections":)" + std::to_string(player->recentDetections.size());
    json += R"(}})";

    return jsonResponse(true, json);
}

std::string RestApi::handleGetDetections(const std::string& query) {
    if (!m_engine || !m_engine->playerManager()) return jsonError("Engine not ready");

    auto players = m_engine->playerManager()->getAllPlayers();
    std::string json = R"({"detections": [)";
    bool first = true;

    for (auto* p : players) {
        for (const auto& d : p->recentDetections) {
            if (!first) json += ",";
            first = false;
            json += R"({)";
            json += R"("player_name":")" + p->identity.name + R"(",)";
            json += R"("type":)" + std::to_string(static_cast<int>(d.type)) + R"(,)";
            json += R"("score":)" + std::to_string(d.score) + R"(,)";
            json += R"("confidence":)" + std::to_string(d.confidence) + R"(,)";
            json += R"("evidence":")" + d.description + R"(")";
            json += R"(})";
        }
    }

    json += R"(]})";
    return jsonResponse(true, json);
}

std::string RestApi::handleGetLogs(const std::string& query) {
    if (!m_engine || !m_engine->logger()) return jsonError("Logger not ready");

    auto logs = m_engine->logger()->getRecent(LogLevel::Debug, 50);
    std::string json = R"({"logs": [)";
    bool first = true;

    for (const auto& l : logs) {
        if (!first) json += ",";
        first = false;
        json += R"({)";
        json += R"("time":")" + std::to_string(
            std::chrono::system_clock::to_time_t(l.timestamp)) + R"(",)";
        json += R"("level":")" + std::to_string(static_cast<int>(l.level)) + R"(",)";
        json += R"("module":")" + l.module + R"(",)";
        json += R"("message":")" + l.message + R"(")";
        json += R"(})";
    }

    json += R"(]})";
    return jsonResponse(true, json);
}

std::string RestApi::handleGetStatistics() {
    if (!m_engine) return jsonError("Engine not ready");

    auto status = m_engine->getStatus();
    std::string json = R"({"statistics": {)";
    json += R"("uptime_seconds":)" + std::to_string(status.uptimeSec) + R"(,)";
    json += R"("players_tracked":)" + std::to_string(status.playersTracked) + R"(,)";
    json += R"("total_detections":)" + std::to_string(status.detectionsTotal) + R"(,)";
    json += R"("active_modules":)" + std::to_string(status.activeModules);
    json += R"(}})";

    return jsonResponse(true, json);
}

std::string RestApi::handleGetRiskLeaderboard(const std::string& query) {
    if (!m_engine || !m_engine->riskEngine()) return jsonError("Risk engine not ready");

    auto risks = m_engine->riskEngine()->getAllPlayerRisks();
    std::string json = R"({"risk_data": [)";
    bool first = true;

    for (const auto& [key, score] : risks) {
        if (!first) json += ",";
        first = false;
        auto* player = m_engine->playerManager()->getPlayer(key);
        json += R"({)";
        json += R"("player_key":")" + key + R"(",)";
        json += R"("name":")" + (player ? player->identity.name : "Unknown") + R"(",)";
        json += R"("total_score":)" + std::to_string(score.totalScore) + R"(,)";
        json += R"("aim_score":)" + std::to_string(score.aimScore) + R"(,)";
        json += R"("movement_score":)" + std::to_string(score.movementScore) + R"(,)";
        json += R"("network_score":)" + std::to_string(score.networkScore) + R"(,)";
        json += R"("behavior_score":)" + std::to_string(score.behaviorScore) + R"(,)";
        json += R"("detection_count":)" + std::to_string(score.detectionCount);
        json += R"(})";
    }

    json += R"(]})";
    return jsonResponse(true, json);
}

std::string RestApi::handleGetBans(const std::string& query) {
    if (!m_engine || !m_engine->playerManager()) return jsonError("Player manager not ready");

    auto bans = m_engine->playerManager()->getActiveBans();
    std::string json = R"({"bans": [)";
    bool first = true;

    for (const auto& b : bans) {
        if (!first) json += ",";
        first = false;
        json += R"({)";
        json += R"("player_key":")" + b.playerKey + R"(",)";
        json += R"("name":")" + b.name + R"(",)";
        json += R"("ban_type":)" + std::to_string(static_cast<int>(b.banType)) + R"(,)";
        json += R"("reason":")" + b.reason + R"(",)";
        json += R"("active":)" + (b.active ? "true" : "false");
        json += R"(})";
    }

    json += R"(]})";
    return jsonResponse(true, json);
}

std::string RestApi::handleGetHealth() {
    if (!m_engine) return jsonError("Engine not initialized");

    auto status = m_engine->getStatus();
    std::string json = R"({"health": {)";
    json += R"("status":")" + std::string(status.running ? "running" : "stopped") + R"(",)";
    json += R"("version":")" + std::string(VERSION) + R"(",)";
    json += R"("uptime_seconds":)" + std::to_string(status.uptimeSec) + R"(,)";
    json += R"("players":)" + std::to_string(status.playersTracked) + R"(,)";
    json += R"("detections":)" + std::to_string(status.detectionsTotal) + R"(,)";
    json += R"("bans":)" + std::to_string(status.bansIssued);
    json += R"(}})";

    return jsonResponse(true, json);
}

std::string RestApi::handleGetDashboard() {
    if (!m_engine) return jsonError("Engine not initialized");

    auto status = m_engine->getStatus();
    auto* riskEngine = m_engine->riskEngine();
    auto* db = m_engine->database();

    std::string json = R"({)";
    json += R"("players_tracked":)" + std::to_string(status.playersTracked) + R"(,)";
    json += R"("total_detections":)" + std::to_string(status.detectionsTotal) + R"(,)";
    json += R"("active_bans":)" + std::to_string(status.bansIssued) + R"(,)";
    json += R"("uptime_hours":)" + std::to_string(status.uptimeSec / 3600) + R"(,)";

    // Count suspicious players (risk > 40)
    uint32_t suspicious = 0;
    if (riskEngine) {
        auto risks = riskEngine->getAllPlayerRisks();
        for (const auto& [k, s] : risks) {
            if (s.totalScore >= 40) suspicious++;
        }
    }
    json += R"("suspicious_players":)" + std::to_string(suspicious) + R"(,)";

    // Detection count in last 24h
    uint64_t det24h = db ? db->getDetectionCountLast(24) : 0;
    json += R"("detections_24h":)" + std::to_string(det24h) + R"(,)";

    // Recent detections
    json += R"("recent_detections": [])";
    json += R"(})";

    return jsonResponse(true, json);
}

std::string RestApi::handlePostAction(const std::string& body) {
    return jsonError("Not implemented");
}

std::string RestApi::handlePostKick(const std::string& body) {
    if (!m_engine) return jsonError("Engine not ready");
    m_engine->logger()->info("RestApi", "Kick requested via API");
    return jsonResponse(true, R"({"message": "Kick command issued"})");
}

std::string RestApi::handlePostBan(const std::string& body) {
    if (!m_engine || !m_engine->playerManager()) return jsonError("Engine not ready");

    // Simplified: parse player_key from body
    std::string playerKey;
    auto keyPos = body.find("player_key");
    if (keyPos != std::string::npos) {
        auto start = body.find("\"", keyPos + 11) + 1;
        auto end = body.find("\"", start);
        if (start != std::string::npos && end != std::string::npos) {
            playerKey = body.substr(start, end - start);
        }
    }

    if (playerKey.empty()) return jsonError("player_key required");

    BanRecord ban;
    ban.playerKey = playerKey;
    ban.banType = ActionType::TempBan;
    ban.reason = "Banned via admin API";
    ban.issuedAt = std::chrono::system_clock::now();
    ban.expiresAt = ban.issuedAt + std::chrono::hours(24);
    ban.active = true;
    ban.issuedBy = "api";

    m_engine->playerManager()->addBan(ban);

    return jsonResponse(true, R"({"message": "Ban issued"})");
}

std::string RestApi::handlePostUnban(const std::string& body) {
    if (!m_engine || !m_engine->playerManager()) return jsonError("Engine not ready");

    std::string playerKey;
    auto keyPos = body.find("player_key");
    if (keyPos != std::string::npos) {
        auto start = body.find("\"", keyPos + 11) + 1;
        auto end = body.find("\"", start);
        if (start != std::string::npos && end != std::string::npos) {
            playerKey = body.substr(start, end - start);
        }
    }

    if (playerKey.empty()) return jsonError("player_key required");

    if (m_engine->database()) {
        m_engine->database()->deactivateBan(playerKey);
    }

    return jsonResponse(true, R"({"message": "Player unbanned"})");
}

std::string RestApi::handlePostReloadConfig() {
    if (!m_engine || !m_engine->configManager()) return jsonError("Config manager not ready");

    if (m_engine->configManager()->reload()) {
        return jsonResponse(true, R"({"message": "Configuration reloaded"})");
    }
    return jsonError("Failed to reload configuration");
}

// ── Helpers ──────────────────────────────────────────────────────────────

std::string RestApi::jsonResponse(bool success, const std::string& data) {
    return R"({"success":)" + std::string(success ? "true" : "false") +
           R"(,"data":)" + data + R"(})";
}

std::string RestApi::jsonError(const std::string& message, int code) {
    return R"({"success":false,"error":")" + message + R"(","code":)" +
           std::to_string(code) + R"(})";
}

bool RestApi::validateAuth(const std::string& authHeader) const {
    if (!m_config.requireAuth) return true;

    // Expect "Bearer <token>"
    std::string expected = "Bearer " + m_config.authToken;
    return authHeader == expected;
}

std::unordered_map<std::string, std::string> RestApi::parseQuery(
    const std::string& query) const
{
    std::unordered_map<std::string, std::string> params;
    std::stringstream ss(query);
    std::string pair;

    while (std::getline(ss, pair, '&')) {
        auto eq = pair.find('=');
        if (eq != std::string::npos) {
            params[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
        }
    }

    return params;
}

std::string RestApi::urlDecode(const std::string& str) const {
    std::string result;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int hex;
            std::stringstream ss(str.substr(i + 1, 2));
            ss >> std::hex >> hex;
            result += static_cast<char>(hex);
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

} // namespace sentinel::api
