// ============================================================================
// Sentinel AntiCheat - SQLite Database Implementation
// ============================================================================
// Implements the IDatabase interface using SQLite3 with thread-safe access,
// prepared statement caching, and automatic schema initialization.
// ============================================================================

#include "SQLiteDatabase.h"
#include <sstream>
#include <filesystem>

namespace sentinel::database {

SQLiteDatabase::SQLiteDatabase(ILogger* logger)
    : m_logger(logger)
{
}

SQLiteDatabase::~SQLiteDatabase() {
    disconnect();
}

bool SQLiteDatabase::connect(const DatabaseConfig& config) {
    std::lock_guard lock(m_mutex);
    m_config = config;

    // Ensure data directory exists
    std::filesystem::path dbPath(config.path);
    auto dir = dbPath.parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }

    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2(config.path.c_str(), &m_db, flags, nullptr);

    if (rc != SQLITE_OK) {
        if (m_logger) {
            m_logger->error("SQLiteDatabase", "Failed to open database: " +
                std::string(sqlite3_errmsg(m_db)));
        }
        return false;
    }

    // Enable WAL mode for better concurrent performance
    execute("PRAGMA journal_mode=WAL");
    execute("PRAGMA synchronous=NORMAL");
    execute("PRAGMA foreign_keys=ON");
    execute("PRAGMA busy_timeout=5000");
    execute("PRAGMA cache_size=-8000");  // 8MB cache

    m_connected = true;

    if (m_logger) {
        m_logger->info("SQLiteDatabase", "Connected to database: " + config.path);
    }

    return true;
}

void SQLiteDatabase::disconnect() {
    std::lock_guard lock(m_mutex);
    finalizeAll();

    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }

    m_connected = false;
}

bool SQLiteDatabase::isConnected() const noexcept {
    return m_connected && m_db != nullptr;
}

bool SQLiteDatabase::initializeSchema() {
    std::lock_guard lock(m_mutex);
    return execute(SCHEMA_SQL);
}

// ── Player Operations ───────────────────────────────────────────────────

bool SQLiteDatabase::savePlayer(const PlayerIdentity& identity) {
    std::lock_guard lock(m_mutex);
    const char* sql = R"(
        INSERT INTO players (steam_id, community_id, steam_id3, steam_id64, name, ip_address, last_seen)
        VALUES (?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
        ON CONFLICT(steam_id) DO UPDATE SET
            name = excluded.name,
            ip_address = excluded.ip_address,
            last_seen = CURRENT_TIMESTAMP
    )";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return false;

    sqlite3_bind_text(stmt, 1, identity.key().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, identity.steamId.communityId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, identity.steamId.steamId3.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, identity.steamId.steamId64.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, identity.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, identity.ipAddress.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = executePrepared(stmt);
    sqlite3_finalize(stmt);
    return ok;
}

std::optional<PlayerIdentity> SQLiteDatabase::loadPlayer(const std::string& playerKey) {
    std::lock_guard lock(m_mutex);
    const char* sql = "SELECT * FROM players WHERE steam_id = ?";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return std::nullopt;

    sqlite3_bind_text(stmt, 1, playerKey.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<PlayerIdentity> result;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        PlayerIdentity identity;
        identity.steamId.communityId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        identity.steamId.steamId3 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        identity.steamId.steamId64 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        identity.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        identity.ipAddress = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        result = identity;
    }

    sqlite3_finalize(stmt);
    return result;
}

// ── Detection Operations ─────────────────────────────────────────────────

bool SQLiteDatabase::saveDetection(const DetectionEvent& event) {
    std::lock_guard lock(m_mutex);
    const char* sql = R"(
        INSERT INTO detections (player_key, detection_type, severity, score, description, module_name, confidence)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return false;

    sqlite3_bind_text(stmt, 1, event.playerKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, static_cast<int>(event.type));
    sqlite3_bind_int(stmt, 3, static_cast<int>(event.severity));
    sqlite3_bind_int(stmt, 4, static_cast<int>(event.score));
    sqlite3_bind_text(stmt, 5, event.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, event.moduleName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 7, static_cast<double>(event.confidence));

    bool ok = executePrepared(stmt);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<DetectionEvent> SQLiteDatabase::loadDetections(
    const std::string& playerKey, size_t limit, size_t offset)
{
    std::lock_guard lock(m_mutex);
    const char* sql = "SELECT * FROM detections WHERE player_key = ? ORDER BY timestamp DESC LIMIT ? OFFSET ?";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return {};

    sqlite3_bind_text(stmt, 1, playerKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(limit));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(offset));

    std::vector<DetectionEvent> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DetectionEvent event;
        event.playerKey = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        event.type = static_cast<DetectionType>(sqlite3_column_int(stmt, 2));
        event.severity = static_cast<RiskLevel>(sqlite3_column_int(stmt, 3));
        event.score = static_cast<uint32_t>(sqlite3_column_int(stmt, 4));
        event.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        event.moduleName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        event.confidence = static_cast<float>(sqlite3_column_double(stmt, 7));
        results.push_back(event);
    }

    sqlite3_finalize(stmt);
    return results;
}

// ── Ban Operations ──────────────────────────────────────────────────────

bool SQLiteDatabase::saveBan(const BanRecord& ban) {
    std::lock_guard lock(m_mutex);
    const char* sql = R"(
        INSERT INTO bans (player_key, name, ip_address, ban_type, reason, issued_at, expires_at, issued_by, active)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, 1)
    )";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return false;

    sqlite3_bind_text(stmt, 1, ban.playerKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ban.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ban.ipAddress.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, static_cast<int>(ban.banType));
    sqlite3_bind_text(stmt, 5, ban.reason.c_str(), -1, SQLITE_TRANSIENT);

    auto issuedAt = std::chrono::system_clock::to_time_t(ban.issuedAt);
    auto expiresAt = std::chrono::system_clock::to_time_t(ban.expiresAt);
    sqlite3_bind_int64(stmt, 6, issuedAt);
    sqlite3_bind_int64(stmt, 7, expiresAt);
    sqlite3_bind_text(stmt, 8, ban.issuedBy.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = executePrepared(stmt);
    sqlite3_finalize(stmt);
    return ok;
}

std::optional<BanRecord> SQLiteDatabase::loadActiveBan(const std::string& playerKey) {
    std::lock_guard lock(m_mutex);
    const char* sql = "SELECT * FROM bans WHERE player_key = ? AND active = 1 ORDER BY issued_at DESC LIMIT 1";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return std::nullopt;

    sqlite3_bind_text(stmt, 1, playerKey.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<BanRecord> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        BanRecord ban;
        ban.playerKey = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        ban.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        ban.ipAddress = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        ban.banType = static_cast<ActionType>(sqlite3_column_int(stmt, 4));
        ban.reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        ban.active = sqlite3_column_int(stmt, 9) != 0;
        result = ban;
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<BanRecord> SQLiteDatabase::loadActiveBans() {
    std::lock_guard lock(m_mutex);
    const char* sql = "SELECT * FROM bans WHERE active = 1 ORDER BY issued_at DESC";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return {};

    std::vector<BanRecord> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        BanRecord ban;
        ban.playerKey = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        ban.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        ban.ipAddress = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        ban.banType = static_cast<ActionType>(sqlite3_column_int(stmt, 4));
        ban.reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        ban.active = sqlite3_column_int(stmt, 9) != 0;
        results.push_back(ban);
    }

    sqlite3_finalize(stmt);
    return results;
}

bool SQLiteDatabase::deactivateBan(const std::string& playerKey) {
    std::lock_guard lock(m_mutex);
    const char* sql = "UPDATE bans SET active = 0 WHERE player_key = ? AND active = 1";
    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return false;

    sqlite3_bind_text(stmt, 1, playerKey.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = executePrepared(stmt);
    sqlite3_finalize(stmt);
    return ok;
}

// ── Stats Operations ────────────────────────────────────────────────────

bool SQLiteDatabase::saveStats(const std::string& playerKey, const PlayerStats& stats) {
    std::lock_guard lock(m_mutex);
    const char* sql = R"(
        INSERT INTO stats (player_key, kills, deaths, headshots, shots, hits, damage,
                          accuracy, headshot_ratio, avg_reaction_ms, avg_kill_dist,
                          kill_streak, best_kill_streak, rounds_played)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return false;

    sqlite3_bind_text(stmt, 1, playerKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, stats.kills);
    sqlite3_bind_int(stmt, 3, stats.deaths);
    sqlite3_bind_int(stmt, 4, stats.headshots);
    sqlite3_bind_int(stmt, 5, stats.shots);
    sqlite3_bind_int(stmt, 6, stats.hits);
    sqlite3_bind_int(stmt, 7, stats.damage);
    sqlite3_bind_double(stmt, 8, stats.accuracy);
    sqlite3_bind_double(stmt, 9, stats.headshotRatio);
    sqlite3_bind_double(stmt, 10, stats.avgReactionMs);
    sqlite3_bind_double(stmt, 11, stats.avgKillDistance);
    sqlite3_bind_int(stmt, 12, stats.killStreak);
    sqlite3_bind_int(stmt, 13, stats.bestKillStreak);
    sqlite3_bind_int(stmt, 14, stats.roundsPlayed);

    bool ok = executePrepared(stmt);
    sqlite3_finalize(stmt);
    return ok;
}

std::optional<PlayerStats> SQLiteDatabase::loadStats(const std::string& playerKey) {
    std::lock_guard lock(m_mutex);
    const char* sql = "SELECT * FROM stats WHERE player_key = ? ORDER BY recorded_at DESC LIMIT 1";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return std::nullopt;

    sqlite3_bind_text(stmt, 1, playerKey.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<PlayerStats> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        PlayerStats stats;
        stats.kills = sqlite3_column_int(stmt, 2);
        stats.deaths = sqlite3_column_int(stmt, 3);
        stats.headshots = sqlite3_column_int(stmt, 4);
        stats.shots = sqlite3_column_int(stmt, 5);
        stats.hits = sqlite3_column_int(stmt, 6);
        stats.damage = sqlite3_column_int(stmt, 7);
        stats.accuracy = static_cast<float>(sqlite3_column_double(stmt, 8));
        stats.headshotRatio = static_cast<float>(sqlite3_column_double(stmt, 9));
        stats.avgReactionMs = static_cast<float>(sqlite3_column_double(stmt, 10));
        stats.avgKillDistance = static_cast<float>(sqlite3_column_double(stmt, 11));
        stats.killStreak = sqlite3_column_int(stmt, 12);
        stats.bestKillStreak = sqlite3_column_int(stmt, 13);
        stats.roundsPlayed = sqlite3_column_int(stmt, 14);
        result = stats;
    }

    sqlite3_finalize(stmt);
    return result;
}

// ── Risk Operations ─────────────────────────────────────────────────────

bool SQLiteDatabase::saveRiskScore(const std::string& playerKey, const RiskScore& score) {
    std::lock_guard lock(m_mutex);
    const char* sql = R"(
        INSERT INTO risk_scores (player_key, total_score, aim_score, movement_score,
                                network_score, behavior_score, detection_count, risk_level)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return false;

    sqlite3_bind_text(stmt, 1, playerKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, score.totalScore);
    sqlite3_bind_int(stmt, 3, score.aimScore);
    sqlite3_bind_int(stmt, 4, score.movementScore);
    sqlite3_bind_int(stmt, 5, score.networkScore);
    sqlite3_bind_int(stmt, 6, score.behaviorScore);
    sqlite3_bind_int(stmt, 7, score.detectionCount);
    sqlite3_bind_int(stmt, 8, static_cast<int>(score.level));

    bool ok = executePrepared(stmt);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<RiskScore> SQLiteDatabase::loadRiskHistory(
    const std::string& playerKey, size_t limit)
{
    std::lock_guard lock(m_mutex);
    const char* sql = "SELECT * FROM risk_scores WHERE player_key = ? ORDER BY recorded_at DESC LIMIT ?";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return {};

    sqlite3_bind_text(stmt, 1, playerKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(limit));

    std::vector<RiskScore> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RiskScore score;
        score.totalScore = sqlite3_column_int(stmt, 2);
        score.aimScore = sqlite3_column_int(stmt, 3);
        score.movementScore = sqlite3_column_int(stmt, 4);
        score.networkScore = sqlite3_column_int(stmt, 5);
        score.behaviorScore = sqlite3_column_int(stmt, 6);
        score.detectionCount = sqlite3_column_int(stmt, 7);
        score.level = static_cast<RiskLevel>(sqlite3_column_int(stmt, 8));
        score.update();
        results.push_back(score);
    }

    sqlite3_finalize(stmt);
    return results;
}

// ── Query Operations ────────────────────────────────────────────────────

std::vector<std::pair<std::string, RiskScore>> SQLiteDatabase::getTopRiskPlayers(size_t limit) {
    std::lock_guard lock(m_mutex);
    const char* sql = R"(
        SELECT r.player_key, r.total_score, r.aim_score, r.movement_score,
               r.network_score, r.behavior_score, r.detection_count, r.risk_level
        FROM risk_scores r
        INNER JOIN (
            SELECT player_key, MAX(recorded_at) as max_time
            FROM risk_scores GROUP BY player_key
        ) latest ON r.player_key = latest.player_key AND r.recorded_at = latest.max_time
        ORDER BY r.total_score DESC LIMIT ?
    )";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return {};

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(limit));

    std::vector<std::pair<std::string, RiskScore>> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RiskScore score;
        score.totalScore = sqlite3_column_int(stmt, 1);
        score.aimScore = sqlite3_column_int(stmt, 2);
        score.movementScore = sqlite3_column_int(stmt, 3);
        score.networkScore = sqlite3_column_int(stmt, 4);
        score.behaviorScore = sqlite3_column_int(stmt, 5);
        score.detectionCount = sqlite3_column_int(stmt, 6);
        score.level = static_cast<RiskLevel>(sqlite3_column_int(stmt, 7));
        score.update();

        std::string playerKey = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        results.emplace_back(playerKey, score);
    }

    sqlite3_finalize(stmt);
    return results;
}

uint64_t SQLiteDatabase::getTotalDetectionCount() {
    std::lock_guard lock(m_mutex);
    const char* sql = "SELECT COUNT(*) FROM detections";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return 0;

    uint64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

uint64_t SQLiteDatabase::getTotalBanCount() {
    std::lock_guard lock(m_mutex);
    const char* sql = "SELECT COUNT(*) FROM bans";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return 0;

    uint64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

uint64_t SQLiteDatabase::getDetectionCountLast(uint32_t hours) {
    std::lock_guard lock(m_mutex);
    const char* sql = "SELECT COUNT(*) FROM detections WHERE timestamp >= datetime('now', ?)";

    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return 0;

    std::string modifier = "-" + std::to_string(hours) + " hours";
    sqlite3_bind_text(stmt, 1, modifier.c_str(), -1, SQLITE_TRANSIENT);

    uint64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

std::string SQLiteDatabase::getDashboardStats() {
    std::lock_guard lock(m_mutex);

    std::string json = R"({
        "players_tracked": )" + std::to_string(getTotalDetectionCount()) + R"(,
        "total_detections": )" + std::to_string(getTotalDetectionCount()) + R"(,
        "active_bans": )" + std::to_string(getTotalBanCount()) + R"(,
        "detections_24h": )" + std::to_string(getDetectionCountLast(24)) + R"(
    })";

    return json;
}

// ── Maintenance ─────────────────────────────────────────────────────────

bool SQLiteDatabase::optimize() {
    std::lock_guard lock(m_mutex);
    return execute("VACUUM") && execute("ANALYZE");
}

bool SQLiteDatabase::purge(uint32_t daysOld) {
    std::lock_guard lock(m_mutex);
    std::string sql = "DELETE FROM detections WHERE timestamp < datetime('now', '-" +
                      std::to_string(daysOld) + " days')";
    return execute(sql);
}

uint64_t SQLiteDatabase::getDatabaseSize() const noexcept {
    if (!m_db) return 0;
    std::filesystem::path dbPath(m_config.path);
    try {
        if (std::filesystem::exists(dbPath)) {
            return std::filesystem::file_size(dbPath);
        }
    } catch (...) {}
    return 0;
}

// ── Private Helpers ─────────────────────────────────────────────────────

bool SQLiteDatabase::execute(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        if (m_logger && m_config.debug) {
            m_logger->error("SQLiteDatabase", "SQL error: " + std::string(errMsg));
        }
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

sqlite3_stmt* SQLiteDatabase::prepare(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        if (m_logger && m_config.debug) {
            m_logger->error("SQLiteDatabase", "Prepare error: " +
                std::string(sqlite3_errmsg(m_db)));
        }
        return nullptr;
    }

    return stmt;
}

bool SQLiteDatabase::executePrepared(sqlite3_stmt* stmt) {
    int rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        if (m_logger && m_config.debug) {
            m_logger->error("SQLiteDatabase", "Step error: " +
                std::string(sqlite3_errmsg(m_db)));
        }
        return false;
    }

    return true;
}

void SQLiteDatabase::finalizeAll() {
    // All prepared statements are finalized after each use in this implementation
    // so nothing extra needed here
}

} // namespace sentinel::database
