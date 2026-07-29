// ============================================================================
// Sentinel AntiCheat - ReHLDS / Metamod Plugin Entry Point
// ============================================================================
// Plugin entry point for the ReHLDS/Metamod platform. Implements the
// Metamod plugin interface and hooks into game events.
// Platforms: ReHLDS, ReGameDLL, ReAPI
// ============================================================================

#include "core/CoreEngine.h"
#include "core/IAntiCheatEngine.h"
#include <string>
#include <cstring>

// ── Metamod Interface (simplified stubs) ────────────────────────────────
// In production, these would use the actual Metamod API:
//   #include <metamod/engine.h>
//   #include <metamod/gamedll.h>
//   #include <metamod/meta_api.h>

namespace sentinel::rehlds {

/// Plugin information structure
struct PluginInfo {
    const char* name        = "Sentinel AntiCheat";
    const char* version     = "1.0.0";
    const char* author      = "Sentinel Team";
    const char* description = "Professional server-side anti-cheat for Counter-Strike 1.6";
    const char* url         = "https://github.com/sentinel-anticheat";
    const char* logTag      = "SENTINEL";
};

static PluginInfo g_pluginInfo;
static bool g_initialized = false;

// ── Engine Callbacks ─────────────────────────────────────────────────────

/// Called when the game starts (server activation)
void OnGameInit() {
    if (g_initialized) return;

    auto& engine = core::CoreEngine::getInstance();
    EngineConfig cfg;
    cfg.configPath = "addons/sentinel/config.yaml";
    cfg.logPath = "addons/sentinel/logs/";
    cfg.dataPath = "addons/sentinel/data/";
    cfg.enableApi = true;

    if (engine.initialize(cfg)) {
        engine.start();
        g_initialized = true;

        // Register the engine as the singleton instance
        // IAntiCheatEngine::instance() now returns &engine
    }
}

/// Called when the game shuts down (server shutdown)
void OnGameShutdown() {
    if (!g_initialized) return;

    auto& engine = core::CoreEngine::getInstance();
    engine.shutdown();
    g_initialized = false;
}

/// Called when a player connects
void OnPlayerConnect(int userId, const char* name, const char* ip, const char* steamId) {
    if (!g_initialized) return;

    PlayerIdentity identity;
    identity.name = name ? name : "Unknown";
    identity.ipAddress = ip ? ip : "0.0.0.0";
    identity.userId = static_cast<uint16_t>(userId);

    if (steamId) {
        identity.steamId.communityId = steamId;
        // Parse SteamID
        // Format: STEAM_X:Y:Z
        // steamId64 = 76561197960265728 + Z * 2 + Y
    }

    auto& engine = core::CoreEngine::getInstance();
    engine.onPlayerConnect(identity);
}

/// Called when a player disconnects
void OnPlayerDisconnect(int userId, const char* reason) {
    if (!g_initialized) return;

    auto& engine = core::CoreEngine::getInstance();
    auto* player = engine.playerManager()->getPlayerByUserId(static_cast<uint16_t>(userId));
    if (player) {
        engine.onPlayerDisconnect(player->identity.key());
    }
}

/// Called when a player kills another
void OnPlayerKill(int killerUserId, int victimUserId, const char* weapon, bool headshot) {
    if (!g_initialized) return;

    auto& engine = core::CoreEngine::getInstance();
    auto* killer = engine.playerManager()->getPlayerByUserId(static_cast<uint16_t>(killerUserId));
    auto* victim = engine.playerManager()->getPlayerByUserId(static_cast<uint16_t>(victimUserId));

    if (killer && victim) {
        engine.onPlayerKill(killer->identity.key(), victim->identity.key(),
                            weapon ? weapon : "unknown", headshot);
    }
}

/// Called when a player takes damage
void OnPlayerDamage(int attackerUserId, int victimUserId, float damage, const char* weapon) {
    if (!g_initialized) return;

    auto& engine = core::CoreEngine::getInstance();
    auto* attacker = engine.playerManager()->getPlayerByUserId(static_cast<uint16_t>(attackerUserId));
    auto* victim = engine.playerManager()->getPlayerByUserId(static_cast<uint16_t>(victimUserId));

    if (attacker && victim) {
        engine.onPlayerDamage(attacker->identity.key(), victim->identity.key(),
                              static_cast<uint32_t>(damage), weapon ? weapon : "unknown");
    }
}

/// Called every server frame
void OnServerFrame(float deltaTime) {
    if (!g_initialized) return;

    auto& engine = core::CoreEngine::getInstance();
    engine.update(deltaTime);
}

/// Called on round start
void OnRoundStart() {
    if (!g_initialized) return;
    core::CoreEngine::getInstance().onRoundStart();
}

/// Called on round end
void OnRoundEnd(int winnerTeam) {
    if (!g_initialized) return;
    core::CoreEngine::getInstance().onRoundEnd(static_cast<uint8_t>(winnerTeam));
}

/// Called on map change
void OnMapChange(const char* mapName) {
    if (!g_initialized) return;
    core::CoreEngine::getInstance().onMapChange(mapName ? mapName : "unknown");
}

/// Handle server console command
bool OnServerCommand(const char* cmd, const char* args) {
    if (!g_initialized) return false;
    return core::CoreEngine::getInstance().executeCommand(cmd ? cmd : "", args ? args : "");
}

/// Get plugin info
const PluginInfo& GetPluginInfo() {
    return g_pluginInfo;
}

/// Check if plugin is initialized
bool IsInitialized() {
    return g_initialized;
}

} // namespace sentinel::rehlds

// ── Metamod Plugin Entry Points ─────────────────────────────────────────
// These use C linkage for compatibility with Metamod's dynamic loading

extern "C" {

/// Metamod plugin info function
/// Returns plugin metadata to Metamod
void Meta_PluginInfo() {
    // In production: RETURN_META_VALUE(MRES_IGNORE, ...)
}

/// Metamod plugin attach function
/// Called when Metamod loads the plugin
int Meta_Attach() {
    sentinel::rehlds::OnGameInit();
    return 1; // Success
}

/// Metamod plugin detach function
/// Called when Metamod unloads the plugin
int Meta_Detach() {
    sentinel::rehlds::OnGameShutdown();
    return 1; // Success
}

} // extern "C"
