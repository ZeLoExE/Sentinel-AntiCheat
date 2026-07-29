// ============================================================================
// Sentinel AntiCheat - SQLite Database Implementation
// ============================================================================
// Default database backend using SQLite. Implements the IDatabase interface.
// Provides all persistence operations for players, detections, bans, etc.
// ============================================================================

#pragma once
#include "core/IDatabase.h"
#include "core/ILogger.h"
#include <sqlite3.h>
#include <string>
#include <mutex>

namespace sentinel::database {

/// SQLite Database Implementation
class SQLiteDatabase final : public IDatabase {
public:
    explicit SQLiteDatabase(ILogger* logger);
    ~SQLiteDatabase() override;

    // ── IDatabase Implementation ─────────────────────────────────────────
    bool connect(const DatabaseConfig& config) override;
    void disconnect() override;
    [[nodiscard]] bool isConnected() const noexcept override;
    bool initializeSchema() override;

    bool savePlayer(const PlayerIdentity& identity) override;
    [[nodiscard]] std::optional<PlayerIdentity> loadPlayer(
        const std::string& playerKey
    ) override;

    bool saveDetection(const DetectionEvent& event) override;
    [[nodiscard]] std::vector<DetectionEvent> loadDetections(
        const std::string& playerKey,
        size_t limit = 100,
        size_t offset = 0
    ) override;

    bool saveBan(const BanRecord& ban) override;
    [[nodiscard]] std::optional<BanRecord> loadActiveBan(
        const std::string& playerKey
    ) override;
    [[nodiscard]] std::vector<BanRecord> loadActiveBans() override;
    bool deactivateBan(const std::string& playerKey) override;

    bool saveStats(const std::string& playerKey,
                    const PlayerStats& stats) override;
    [[nodiscard]] std::optional<PlayerStats> loadStats(
        const std::string& playerKey
    ) override;

    bool saveRiskScore(const std::string& playerKey,
                        const RiskScore& score) override;
    [[nodiscard]] std::vector<RiskScore> loadRiskHistory(
        const std::string& playerKey,
        size_t limit = 50
    ) override;

    [[nodiscard]] std::vector<std::pair<std::string, RiskScore>>
        getTopRiskPlayers(size_t limit = 20) override;
    [[nodiscard]] uint64_t getTotalDetectionCount() override;
    [[nodiscard]] uint64_t getTotalBanCount() override;
    [[nodiscard]] uint64_t getDetectionCountLast(uint32_t hours) override;
    [[nodiscard]] std::string getDashboardStats() override;

    bool optimize() override;
    bool purge(uint32_t daysOld) override;
    [[nodiscard]] uint64_t getDatabaseSize() const noexcept override;

private:
    /// Execute a query with no result set
    bool execute(const std::string& sql);

    /// Execute a query with parameters
    bool executePrepared(sqlite3_stmt* stmt);

    /// Create the prepared statement cache
    sqlite3_stmt* prepare(const std::string& sql);

    /// Finalize all prepared statements
    void finalizeAll();

    ILogger*            m_logger;
    sqlite3*            m_db        = nullptr;
    DatabaseConfig      m_config;
    bool                m_connected = false;
    mutable std::mutex  m_mutex;

    /// Schema SQL statements
    static constexpr const char* SCHEMA_SQL = R"(
        CREATE TABLE IF NOT EXISTS players (
            steam_id        TEXT PRIMARY KEY,
            community_id    TEXT,
            steam_id3       TEXT,
            steam_id64      TEXT,
            name            TEXT NOT NULL,
            ip_address      TEXT,
            first_seen      DATETIME DEFAULT CURRENT_TIMESTAMP,
            last_seen       DATETIME,
            total_kills     INTEGER DEFAULT 0,
            total_deaths    INTEGER DEFAULT 0,
            total_headshots INTEGER DEFAULT 0,
            total_damage    INTEGER DEFAULT 0,
            warnings_given  INTEGER DEFAULT 0,
            flags           TEXT DEFAULT ''
        );

        CREATE TABLE IF NOT EXISTS detections (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            player_key      TEXT NOT NULL,
            detection_type  INTEGER NOT NULL,
            severity        INTEGER NOT NULL,
            score           INTEGER NOT NULL,
            description     TEXT,
            module_name     TEXT,
            confidence      REAL DEFAULT 0.0,
            timestamp       DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (player_key) REFERENCES players(steam_id)
        );

        CREATE INDEX IF NOT EXISTS idx_detections_player
            ON detections(player_key);
        CREATE INDEX IF NOT EXISTS idx_detections_timestamp
            ON detections(timestamp);

        CREATE TABLE IF NOT EXISTS bans (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            player_key      TEXT NOT NULL,
            name            TEXT,
            ip_address      TEXT,
            ban_type        INTEGER NOT NULL,
            reason          TEXT,
            issued_at       DATETIME DEFAULT CURRENT_TIMESTAMP,
            expires_at      DATETIME,
            issued_by       TEXT DEFAULT 'system',
            active          INTEGER DEFAULT 1,
            FOREIGN KEY (player_key) REFERENCES players(steam_id)
        );

        CREATE INDEX IF NOT EXISTS idx_bans_player
            ON bans(player_key);
        CREATE INDEX IF NOT EXISTS idx_bans_active
            ON bans(active);

        CREATE TABLE IF NOT EXISTS stats (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            player_key      TEXT NOT NULL,
            kills           INTEGER DEFAULT 0,
            deaths          INTEGER DEFAULT 0,
            headshots       INTEGER DEFAULT 0,
            shots           INTEGER DEFAULT 0,
            hits            INTEGER DEFAULT 0,
            damage          INTEGER DEFAULT 0,
            accuracy        REAL DEFAULT 0.0,
            headshot_ratio  REAL DEFAULT 0.0,
            avg_reaction_ms REAL DEFAULT 0.0,
            avg_kill_dist   REAL DEFAULT 0.0,
            kill_streak     INTEGER DEFAULT 0,
            best_kill_streak INTEGER DEFAULT 0,
            rounds_played   INTEGER DEFAULT 0,
            recorded_at     DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (player_key) REFERENCES players(steam_id)
        );

        CREATE TABLE IF NOT EXISTS risk_scores (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            player_key      TEXT NOT NULL,
            total_score     INTEGER DEFAULT 0,
            aim_score       INTEGER DEFAULT 0,
            movement_score  INTEGER DEFAULT 0,
            network_score   INTEGER DEFAULT 0,
            behavior_score  INTEGER DEFAULT 0,
            detection_count INTEGER DEFAULT 0,
            risk_level      INTEGER DEFAULT 0,
            recorded_at     DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (player_key) REFERENCES players(steam_id)
        );

        CREATE INDEX IF NOT EXISTS idx_risk_player
            ON risk_scores(player_key);
        CREATE INDEX IF NOT EXISTS idx_risk_total
            ON risk_scores(total_score DESC);

        CREATE TABLE IF NOT EXISTS config (
            key             TEXT PRIMARY KEY,
            value           TEXT NOT NULL,
            description     TEXT,
            updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS admin_log (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            admin_key       TEXT,
            action          TEXT NOT NULL,
            target_player   TEXT,
            details         TEXT,
            timestamp       DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )";
};

} // namespace sentinel::database
