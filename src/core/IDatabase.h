// ============================================================================
// Sentinel AntiCheat - Database Interface
// ============================================================================
// Abstract persistence layer. Default implementation uses SQLite.
// Supports future migration to PostgreSQL or other backends.
// ============================================================================

#pragma once
#include "Types.h"
#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace sentinel {

/// Database connection configuration
struct DatabaseConfig {
    std::string type     = "sqlite";   // "sqlite" or "postgresql"
    std::string host     = "localhost";
    uint16_t    port     = 5432;
    std::string name     = "sentinel_anticheat";
    std::string user     = "sentinel";
    std::string password = "";
    std::string path     = "sentinel.db";  // For SQLite
    uint32_t    poolSize = 4;
    bool        debug    = false;
};

/// Database Interface
class IDatabase {
public:
    virtual ~IDatabase() = default;

    /// Connect to the database
    virtual bool connect(const DatabaseConfig& config) = 0;

    /// Disconnect
    virtual void disconnect() = 0;

    /// Check if connected
    [[nodiscard]] virtual bool isConnected() const noexcept = 0;

    /// Initialize schema (create tables)
    virtual bool initializeSchema() = 0;

    // ── Player Operations ───────────────────────────────────────────────

    /// Save or update player profile
    virtual bool savePlayer(const PlayerIdentity& identity) = 0;

    /// Load player profile
    [[nodiscard]] virtual std::optional<PlayerIdentity> loadPlayer(
        const std::string& playerKey
    ) = 0;

    // ── Detection Operations ────────────────────────────────────────────

    /// Save a detection event
    virtual bool saveDetection(const DetectionEvent& event) = 0;

    /// Load recent detections for a player
    [[nodiscard]] virtual std::vector<DetectionEvent> loadDetections(
        const std::string& playerKey,
        size_t limit = 100,
        size_t offset = 0
    ) = 0;

    // ── Ban Operations ──────────────────────────────────────────────────

    /// Save a ban record
    virtual bool saveBan(const BanRecord& ban) = 0;

    /// Load active ban for a player
    [[nodiscard]] virtual std::optional<BanRecord> loadActiveBan(
        const std::string& playerKey
    ) = 0;

    /// Load all active bans
    [[nodiscard]] virtual std::vector<BanRecord> loadActiveBans() = 0;

    /// Deactivate an expired ban
    virtual bool deactivateBan(const std::string& playerKey) = 0;

    // ── Statistics ──────────────────────────────────────────────────────

    /// Save player statistics
    virtual bool saveStats(const std::string& playerKey, const PlayerStats& stats) = 0;

    /// Load player statistics
    [[nodiscard]] virtual std::optional<PlayerStats> loadStats(
        const std::string& playerKey
    ) = 0;

    // ── Risk Operations ─────────────────────────────────────────────────

    /// Save risk score snapshot
    virtual bool saveRiskScore(
        const std::string& playerKey,
        const RiskScore& score
    ) = 0;

    /// Load risk history for a player
    [[nodiscard]] virtual std::vector<RiskScore> loadRiskHistory(
        const std::string& playerKey,
        size_t limit = 50
    ) = 0;

    // ── Query Operations ────────────────────────────────────────────────

    /// Get players sorted by risk score (descending)
    [[nodiscard]] virtual std::vector<std::pair<std::string, RiskScore>>
        getTopRiskPlayers(size_t limit = 20) = 0;

    /// Get total detection count
    [[nodiscard]] virtual uint64_t getTotalDetectionCount() = 0;

    /// Get total ban count
    [[nodiscard]] virtual uint64_t getTotalBanCount() = 0;

    /// Get detection count in last N hours
    [[nodiscard]] virtual uint64_t getDetectionCountLast(
        uint32_t hours
    ) = 0;

    /// Get statistics for the dashboard
    [[nodiscard]] virtual std::string getDashboardStats() = 0;

    // ── Maintenance ─────────────────────────────────────────────────────

    /// Vacuum/optimize database
    virtual bool optimize() = 0;

    /// Purge old data (older than specified days)
    virtual bool purge(uint32_t daysOld) = 0;

    /// Get database file size (bytes)
    [[nodiscard]] virtual uint64_t getDatabaseSize() const noexcept = 0;
};

using DatabasePtr = std::unique_ptr<IDatabase>;

} // namespace sentinel
