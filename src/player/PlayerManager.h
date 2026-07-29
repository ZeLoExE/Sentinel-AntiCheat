// ============================================================================
// Sentinel AntiCheat - Player Manager Implementation
// ============================================================================
// Manages all connected players, their contexts, statistics, and history.
// Thread-safe storage with O(1) lookup by SteamID, UserID, or entity index.
// ============================================================================

#pragma once
#include "core/IPlayerManager.h"
#include "core/ILogger.h"
#include "core/IDatabase.h"
#include <unordered_map>
#include <shared_mutex>
#include <memory>

namespace sentinel::player {

/// Player Manager Implementation
class PlayerManager final : public IPlayerManager {
public:
    explicit PlayerManager(ILogger* logger, IDatabase* database);

    // ── IPlayerManager Implementation ────────────────────────────────────
    PlayerContext& registerPlayer(const PlayerIdentity& identity) override;

    [[nodiscard]] PlayerContext* getPlayer(
        const std::string& playerKey
    ) override;

    [[nodiscard]] PlayerContext* getPlayerByUserId(uint16_t userId) override;

    [[nodiscard]] PlayerContext* getPlayerByIndex(uint8_t index) override;

    [[nodiscard]] bool hasPlayer(const std::string& playerKey) const override;

    void removePlayer(const std::string& playerKey) override;

    [[nodiscard]] std::vector<PlayerContext*> getAllPlayers() override;

    [[nodiscard]] size_t playerCount() const noexcept override;

    void updateStats(
        const std::string& playerKey,
        const PlayerStats& stats
    ) override;

    void addDetection(
        const std::string& playerKey,
        const DetectionEvent& event
    ) override;

    [[nodiscard]] std::vector<DetectionEvent> getRecentDetections(
        const std::string& playerKey,
        size_t count = 20
    ) const override;

    [[nodiscard]] bool isBanned(const std::string& playerKey) const override;

    void addBan(const BanRecord& ban) override;

    [[nodiscard]] std::optional<BanRecord> getBan(
        const std::string& playerKey
    ) const override;

    [[nodiscard]] std::vector<BanRecord> getActiveBans() const override;

    void clearAll() override;

private:
    ILogger*                              m_logger;
    IDatabase*                            m_database;
    std::unordered_map<std::string, std::unique_ptr<PlayerContext>> m_players;
    std::unordered_map<uint16_t, PlayerContext*> m_userIdMap;
    std::unordered_map<uint8_t, PlayerContext*>  m_indexMap;
    mutable std::shared_mutex             m_mutex;

    /// Convert a player context to a persistable format and save
    void persistPlayer(const PlayerContext& ctx);

    /// Remove player from all lookup maps
    void removeFromMaps(const std::string& playerKey);
};

} // namespace sentinel::player
