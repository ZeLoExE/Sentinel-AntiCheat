// ============================================================================
// Sentinel AntiCheat - AMX Mod X Compatibility Layer
// ============================================================================
// Provides AMXX plugin forwards and native functions for integration.
// Allows AMXX plugins to query detection data, manage bans, and
// register custom detectors through the Sentinel AntiCheat API.
// ============================================================================

#pragma once
#include "core/Types.h"
#include <string>
#include <functional>

namespace sentinel::ammx {

/// AMXX Forward types that plugins can hook into
enum class AmxxForward : uint8_t {
    OnPlayerDetected      = 0,  // Called when a detection event occurs
    OnPlayerRiskChanged   = 1,  // Called when player risk level changes
    OnPlayerAutoAction    = 2,  // Called before an automatic action is taken
    OnBanIssued           = 3,  // Called when a ban is issued
    OnConfigReloaded      = 4,  // Called when config is reloaded
    OnModuleStatusChange  = 5   // Called when a detection module is enabled/disabled
};

/// Forward function signature for AMXX plugins
using AmxxForwardCallback = std::function<void(const std::string& data)>;

/// AMXX Compatibility Manager
/// Bridges between Sentinel's internal API and AMX Mod X plugins.
class AmxxCompat {
public:
    /// Register a new AMXX forward
    static bool RegisterForward(AmxxForward forward, AmxxForwardCallback callback);

    /// Unregister a forward
    static void UnregisterForward(AmxxForward forward);

    /// Execute a forward (calls all registered callbacks)
    static void ExecuteForward(AmxxForward forward, const std::string& data);

    /// ── Native Functions Available to AMXX Plugins ─────────────────────

    /// native sentinel_get_player_risk(const player[])
    static int GetPlayerRisk(const std::string& playerKey);

    /// native sentinel_is_player_banned(const player[])
    static bool IsPlayerBanned(const std::string& playerKey);

    /// native sentinel_ban_player(const player[], const reason[], duration)
    static bool BanPlayer(const std::string& playerKey, const std::string& reason, int duration);

    /// native sentinel_unban_player(const player[])
    static bool UnbanPlayer(const std::string& playerKey);

    /// native sentinel_get_detection_count(const player[])
    static int GetDetectionCount(const std::string& playerKey);

    /// native sentinel_get_player_risk_score(const player[])
    static int GetPlayerRiskScore(const std::string& playerKey);

    /// native sentinel_get_detection_score(const player[])
    static int GetDetectionScore(const std::string& playerKey);

    /// native sentinel_is_module_enabled(const module[])
    static bool IsModuleEnabled(const std::string& moduleName);

    /// native sentinel_set_module_enabled(const module[], bool enabled)
    static bool SetModuleEnabled(const std::string& moduleName, bool enabled);

    /// native sentinel_reload_config()
    static bool ReloadConfig();

    /// native sentinel_get_server_stats()
    static std::string GetServerStats();

    /// AMXX native function registration
    /// Called during AMXX plugin load to expose Sentinel natives to AMXX
    static void RegisterNatives();
};

} // namespace sentinel::ammx
