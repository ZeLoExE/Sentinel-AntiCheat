// ============================================================================
// Sentinel AntiCheat - Player Manager Interface
// ============================================================================
// Manages all player data, statistics, and lifecycle.
// Every connected player has a PlayerContext that persists their data,
// detection history, and current risk assessment.
// ============================================================================

#pragma once
#include "Types.h"
#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace sentinel {

/// Full player context - all data known about a player
struct PlayerContext {
    PlayerIdentity      identity;
    PlayerStats         stats;
    RiskScore           risk;
    Team                team       = Team::Unassigned;
    bool                connected  = false;
    bool                authenticating = true;
    std::chrono::system_clock::time_point joinTime;
    std::chrono::system_clock::time_point lastActivity;
    std::vector<DetectionEvent> recentDetections;
    std::vector<RiskScore>      riskHistory;
    std::vector<std::string>    flags;        // Suspicion flags
    uint32_t            warningsGiven = 0;
    float               latencyMs     = 0.0f;
    float               packetLoss    = 0.0f;
    uint32_t            currentHealth = 100;
    uint32_t            currentArmor  = 0;
    bool                hasShield     = false;
    std::string         currentWeapon;
    Vector3             origin;
    QAngle              viewAngles;
    QAngle              punchAngles;
};

/// Player Manager Interface
class IPlayerManager {
public:
    virtual ~IPlayerManager() = default;

    /// Register or retrieve a player's context
    virtual PlayerContext& registerPlayer(const PlayerIdentity& identity) = 0;

    /// Get player context by key (SteamID or IP)
    [[nodiscard]] virtual PlayerContext* getPlayer(
        const std::string& playerKey
    ) = 0;

    /// Get player context by user ID (server slot)
    [[nodiscard]] virtual PlayerContext* getPlayerByUserId(uint16_t userId) = 0;

    /// Get player context by entity index
    [[nodiscard]] virtual PlayerContext* getPlayerByIndex(uint8_t index) = 0;

    /// Check if a player exists
    [[nodiscard]] virtual bool hasPlayer(const std::string& playerKey) const = 0;

    /// Remove a player (on disconnect)
    virtual void removePlayer(const std::string& playerKey) = 0;

    /// Get all connected players
    [[nodiscard]] virtual std::vector<PlayerContext*> getAllPlayers() = 0;

    /// Get count of connected players
    [[nodiscard]] virtual size_t playerCount() const noexcept = 0;

    /// Update player stats (kills, deaths, etc.)
    virtual void updateStats(
        const std::string& playerKey,
        const PlayerStats& stats
    ) = 0;

    /// Record a detection event for a player
    virtual void addDetection(
        const std::string& playerKey,
        const DetectionEvent& event
    ) = 0;

    /// Get recent detections for a player
    [[nodiscard]] virtual std::vector<DetectionEvent> getRecentDetections(
        const std::string& playerKey,
        size_t count = 20
    ) const = 0;

    /// Check if a player is banned
    [[nodiscard]] virtual bool isBanned(const std::string& playerKey) const = 0;

    /// Register a ban
    virtual void addBan(const BanRecord& ban) = 0;

    /// Get ban record for a player
    [[nodiscard]] virtual std::optional<BanRecord> getBan(
        const std::string& playerKey
    ) const = 0;

    /// Get all active bans
    [[nodiscard]] virtual std::vector<BanRecord> getActiveBans() const = 0;

    /// Clear all player data (map change)
    virtual void clearAll() = 0;
};

using PlayerManagerPtr = std::unique_ptr<IPlayerManager>;

} // namespace sentinel
