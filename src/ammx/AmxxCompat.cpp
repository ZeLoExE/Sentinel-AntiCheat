// ============================================================================
// Sentinel AntiCheat - AMX Mod X Compatibility Layer
// ============================================================================
// Implements forwards and natives for AMXX plugin integration.
// ============================================================================

#include "AmxxCompat.h"
#include "core/CoreEngine.h"
#include <unordered_map>
#include <vector>

namespace sentinel::ammx {

// ── Forward Registry ────────────────────────────────────────────────────
static std::unordered_multimap<AmxxForward, AmxxForwardCallback> g_forwards;

bool AmxxCompat::RegisterForward(AmxxForward forward, AmxxForwardCallback callback) {
    g_forwards.emplace(forward, std::move(callback));
    return true;
}

void AmxxCompat::UnregisterForward(AmxxForward forward) {
    g_forwards.erase(forward);
}

void AmxxCompat::ExecuteForward(AmxxForward forward, const std::string& data) {
    auto range = g_forwards.equal_range(forward);
    for (auto it = range.first; it != range.second; ++it) {
        it->second(data);
    }
}

// ── Native Implementations ──────────────────────────────────────────────

int AmxxCompat::GetPlayerRisk(const std::string& playerKey) {
    auto* engine = core::CoreEngine::instance();
    if (!engine || !engine->riskEngine()) return 0;
    return static_cast<int>(engine->riskEngine()->getPlayerRisk(playerKey).totalScore);
}

bool AmxxCompat::IsPlayerBanned(const std::string& playerKey) {
    auto* engine = core::CoreEngine::instance();
    if (!engine || !engine->playerManager()) return false;
    return engine->playerManager()->isBanned(playerKey);
}

bool AmxxCompat::BanPlayer(const std::string& playerKey, const std::string& reason, int duration) {
    auto* engine = core::CoreEngine::instance();
    if (!engine || !engine->playerManager()) return false;

    BanRecord ban;
    ban.playerKey = playerKey;
    ban.banType = (duration < 0) ? ActionType::PermBan : ActionType::TempBan;
    ban.reason = reason;
    ban.issuedAt = std::chrono::system_clock::now();
    ban.expiresAt = (duration < 0) ?
        ban.issuedAt + std::chrono::hours(99999) :
        ban.issuedAt + std::chrono::minutes(duration);
    ban.active = true;
    ban.issuedBy = "amxx_plugin";

    engine->playerManager()->addBan(ban);

    if (engine->logger()) {
        engine->logger()->logBan(ban);
    }

    return true;
}

bool AmxxCompat::UnbanPlayer(const std::string& playerKey) {
    auto* engine = core::CoreEngine::instance();
    if (!engine || !engine->database()) return false;
    return engine->database()->deactivateBan(playerKey);
}

int AmxxCompat::GetDetectionCount(const std::string& playerKey) {
    auto* engine = core::CoreEngine::instance();
    if (!engine || !engine->playerManager()) return 0;
    auto* player = engine->playerManager()->getPlayer(playerKey);
    return player ? static_cast<int>(player->recentDetections.size()) : 0;
}

int AmxxCompat::GetPlayerRiskScore(const std::string& playerKey) {
    auto* engine = core::CoreEngine::instance();
    if (!engine || !engine->riskEngine()) return 0;
    return static_cast<int>(engine->riskEngine()->getPlayerRisk(playerKey).totalScore);
}

int AmxxCompat::GetDetectionScore(const std::string& playerKey) {
    auto* engine = core::CoreEngine::instance();
    if (!engine || !engine->riskEngine()) return 0;
    return static_cast<int>(engine->riskEngine()->getPlayerRisk(playerKey).totalScore);
}

bool AmxxCompat::IsModuleEnabled(const std::string& moduleName) {
    auto* engine = core::CoreEngine::instance();
    if (!engine) return false;
    auto* detector = engine->getDetector(moduleName);
    return detector ? detector->config().enabled : false;
}

bool AmxxCompat::SetModuleEnabled(const std::string& moduleName, bool enabled) {
    auto* engine = core::CoreEngine::instance();
    if (!engine) return false;
    engine->setDetectorEnabled(moduleName, enabled);
    return true;
}

bool AmxxCompat::ReloadConfig() {
    auto* engine = core::CoreEngine::instance();
    if (!engine || !engine->configManager()) return false;
    return engine->configManager()->reload();
}

std::string AmxxCompat::GetServerStats() {
    auto* engine = core::CoreEngine::instance();
    if (!engine) return "Engine not initialized";
    return engine->status();
}

void AmxxCompat::RegisterNatives() {
    // In production, this would register with the AMXX runtime:
    // RegisterAmxxNative("sentinel_get_player_risk", AmxxCompat::GetPlayerRisk);
    // RegisterAmxxNative("sentinel_is_player_banned", AmxxCompat::IsPlayerBanned);
    // etc.
}

} // namespace sentinel::ammx
