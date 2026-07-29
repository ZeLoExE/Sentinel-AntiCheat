// ============================================================================
// Sentinel AntiCheat - Player Manager Implementation
// ============================================================================
// Manages player contexts, statistics, and detection history.
// Thread-safe with O(1) lookups via SteamID, UserID, or entity index.
// ============================================================================

#include "PlayerManager.h"
#include <algorithm>

namespace sentinel::player {

PlayerManager::PlayerManager(ILogger* logger, IDatabase* database)
    : m_logger(logger)
    , m_database(database)
{
}

PlayerContext& PlayerManager::registerPlayer(const PlayerIdentity& identity) {
    std::unique_lock lock(m_mutex);

    // Check if player already exists
    auto it = m_players.find(identity.key());
    if (it != m_players.end()) {
        // Update existing player
        it->second->identity = identity;
        it->second->connected = true;
        it->second->lastActivity = std::chrono::system_clock::now();

        // Update maps
        m_userIdMap[identity.userId] = it->second.get();
        m_indexMap[identity.index] = it->second.get();

        return *it->second;
    }

    // Create new player context
    auto ctx = std::make_unique<PlayerContext>();
    ctx->identity = identity;
    ctx->connected = true;
    ctx->joinTime = std::chrono::system_clock::now();
    ctx->lastActivity = ctx->joinTime;

    auto* ptr = ctx.get();
    m_players[identity.key()] = std::move(ctx);
    m_userIdMap[identity.userId] = ptr;
    m_indexMap[identity.index] = ptr;

    // Load from database (if exists)
    if (m_database) {
        auto dbPlayer = m_database->loadPlayer(identity.key());
        auto dbBans = m_database->loadActiveBan(identity.key());
        // Ban check would go here
    }

    if (m_logger) {
        m_logger->info("PlayerManager",
            "Registered player: " + identity.name + " (" + identity.key() + ")");
    }

    return *ptr;
}

PlayerContext* PlayerManager::getPlayer(const std::string& playerKey) {
    std::shared_lock lock(m_mutex);
    auto it = m_players.find(playerKey);
    return (it != m_players.end()) ? it->second.get() : nullptr;
}

PlayerContext* PlayerManager::getPlayerByUserId(uint16_t userId) {
    std::shared_lock lock(m_mutex);
    auto it = m_userIdMap.find(userId);
    return (it != m_userIdMap.end()) ? it->second : nullptr;
}

PlayerContext* PlayerManager::getPlayerByIndex(uint8_t index) {
    std::shared_lock lock(m_mutex);
    auto it = m_indexMap.find(index);
    return (it != m_indexMap.end()) ? it->second : nullptr;
}

bool PlayerManager::hasPlayer(const std::string& playerKey) const {
    std::shared_lock lock(m_mutex);
    return m_players.find(playerKey) != m_players.end();
}

void PlayerManager::removePlayer(const std::string& playerKey) {
    std::unique_lock lock(m_mutex);

    auto it = m_players.find(playerKey);
    if (it == m_players.end()) return;

    // Persist player data before removal
    persistPlayer(*it->second);

    // Update leave time
    it->second->connected = false;
    it->second->lastActivity = std::chrono::system_clock::now();

    // Remove from lookup maps
    removeFromMaps(playerKey);

    m_players.erase(it);

    if (m_logger) {
        m_logger->info("PlayerManager", "Removed player: " + playerKey);
    }
}

std::vector<PlayerContext*> PlayerManager::getAllPlayers() {
    std::shared_lock lock(m_mutex);
    std::vector<PlayerContext*> result;
    for (const auto& [key, ctx] : m_players) {
        if (ctx->connected) {
            result.push_back(ctx.get());
        }
    }
    return result;
}

size_t PlayerManager::playerCount() const noexcept {
    std::shared_lock lock(m_mutex);
    return std::count_if(m_players.begin(), m_players.end(),
        [](const auto& pair) { return pair.second->connected; });
}

void PlayerManager::updateStats(const std::string& playerKey, const PlayerStats& stats) {
    std::unique_lock lock(m_mutex);
    auto* ctx = getPlayer(playerKey);
    if (!ctx) return;

    ctx->stats = stats;

    // Update derived stats
    if (ctx->stats.deaths > 0) {
        ctx->stats.kdRatio = static_cast<float>(ctx->stats.kills) /
                             static_cast<float>(ctx->stats.deaths);
    }
    if (ctx->stats.kills > 0) {
        ctx->stats.headshotRatio = static_cast<float>(ctx->stats.headshots) /
                                   static_cast<float>(ctx->stats.kills);
    }
    if (ctx->stats.shots > 0) {
        ctx->stats.accuracy = static_cast<float>(ctx->stats.hits) /
                              static_cast<float>(ctx->stats.shots) * 100.0f;
    }

    // Persist to database
    if (m_database) {
        m_database->saveStats(playerKey, ctx->stats);
    }
}

void PlayerManager::addDetection(const std::string& playerKey, const DetectionEvent& event) {
    std::unique_lock lock(m_mutex);
    auto* ctx = getPlayer(playerKey);
    if (!ctx) return;

    ctx->recentDetections.push_back(event);
    if (ctx->recentDetections.size() > 50) {
        ctx->recentDetections.erase(ctx->recentDetections.begin());
    }
}

std::vector<DetectionEvent> PlayerManager::getRecentDetections(
    const std::string& playerKey, size_t count) const
{
    std::shared_lock lock(m_mutex);
    auto it = m_players.find(playerKey);
    if (it == m_players.end()) return {};

    std::vector<DetectionEvent> result;
    size_t start = (it->second->recentDetections.size() > count)
        ? it->second->recentDetections.size() - count : 0;

    for (size_t i = start; i < it->second->recentDetections.size(); ++i) {
        result.push_back(it->second->recentDetections[i]);
    }
    return result;
}

bool PlayerManager::isBanned(const std::string& playerKey) const {
    if (!m_database) return false;
    auto ban = m_database->loadActiveBan(playerKey);
    return ban.has_value() && ban->active;
}

void PlayerManager::addBan(const BanRecord& ban) {
    if (m_database) {
        m_database->saveBan(ban);
    }
    if (m_logger) {
        m_logger->logBan(ban);
    }
}

std::optional<BanRecord> PlayerManager::getBan(const std::string& playerKey) const {
    if (!m_database) return std::nullopt;
    return m_database->loadActiveBan(playerKey);
}

std::vector<BanRecord> PlayerManager::getActiveBans() const {
    if (!m_database) return {};
    return m_database->loadActiveBans();
}

void PlayerManager::clearAll() {
    std::unique_lock lock(m_mutex);

    // Persist all players before clearing
    for (auto& [key, ctx] : m_players) {
        persistPlayer(*ctx);
    }

    m_players.clear();
    m_userIdMap.clear();
    m_indexMap.clear();

    if (m_logger) {
        m_logger->info("PlayerManager", "All player data cleared");
    }
}

void PlayerManager::persistPlayer(const PlayerContext& ctx) {
    if (!m_database) return;

    m_database->savePlayer(ctx.identity);
    m_database->saveStats(ctx.identity.key(), ctx.stats);

    for (const auto& event : ctx.recentDetections) {
        m_database->saveDetection(event);
    }

    m_database->saveRiskScore(ctx.identity.key(), ctx.risk);
}

void PlayerManager::removeFromMaps(const std::string& playerKey) {
    auto it = m_players.find(playerKey);
    if (it == m_players.end()) return;

    // Remove from user ID map
    for (auto uIt = m_userIdMap.begin(); uIt != m_userIdMap.end(); ) {
        if (uIt->second == it->second.get()) {
            uIt = m_userIdMap.erase(uIt);
        } else {
            ++uIt;
        }
    }

    // Remove from index map
    for (auto iIt = m_indexMap.begin(); iIt != m_indexMap.end(); ) {
        if (iIt->second == it->second.get()) {
            iIt = m_indexMap.erase(iIt);
        } else {
            ++iIt;
        }
    }
}

} // namespace sentinel::player
