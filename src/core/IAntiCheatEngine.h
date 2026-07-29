// ============================================================================
// Sentinel AntiCheat - Main Engine Interface
// ============================================================================
// Top-level orchestrator that wires all subsystems together.
// This is the entry point for the ReHLDS/ReGameDLL plugin.
// ============================================================================

#pragma once
#include "Types.h"
#include "IDetector.h"
#include "IModule.h"
#include "IRiskEngine.h"
#include "IPlayerManager.h"
#include "IDatabase.h"
#include "ILogger.h"
#include "IConfig.h"
#include "IApi.h"
#include <string>
#include <memory>
#include <vector>

namespace sentinel {

/// Engine initialization configuration
struct EngineConfig {
    std::string configPath     = "config/sentinel.yaml";
    std::string logPath        = "logs/";
    std::string dataPath       = "data/";
    bool        autoLoadModules = true;
    bool        enableApi      = true;
    bool        debugMode      = false;
    uint32_t    updateIntervalMs = 100;  // Detection update interval
};

/// Engine status
struct EngineStatus {
    bool        initialized = false;
    bool        running     = false;
    uint32_t    uptimeSec   = 0;
    uint32_t    playersTracked = 0;
    uint32_t    detectionsTotal = 0;
    uint32_t    bansIssued     = 0;
    uint32_t    activeModules  = 0;
    std::string version;
    std::string platform;
};

/// Main Anti-Cheat Engine Interface
/// The engine is a singleton that manages all subsystems.
class IAntiCheatEngine : public IModule {
public:
    virtual ~IAntiCheatEngine() = default;

    /// Initialize the entire anti-cheat system
    /// @param config Engine configuration
    /// @return true if initialization succeeded
    virtual bool initialize(const EngineConfig& config) = 0;

    // ── Subsystem Access ────────────────────────────────────────────────

    virtual IRiskEngine*       riskEngine()       noexcept = 0;
    virtual IPlayerManager*    playerManager()    noexcept = 0;
    virtual IDatabase*         database()         noexcept = 0;
    virtual ILogger*           logger()           noexcept = 0;
    virtual IConfig*           configManager()    noexcept = 0;
    virtual IApi*              api()              noexcept = 0;

    // ── Module Management ───────────────────────────────────────────────

    /// Register a new module
    virtual bool registerModule(ModulePtr module) = 0;

    /// Get a module by name
    [[nodiscard]] virtual IModule* getModule(
        const std::string& name
    ) = 0;

    /// Get all registered modules
    [[nodiscard]] virtual std::vector<IModule*> getAllModules() = 0;

    // ── Detector Management ─────────────────────────────────────────────

    /// Register a new detector
    virtual bool registerDetector(DetectorPtr detector) = 0;

    /// Get a detector by name
    [[nodiscard]] virtual IDetector* getDetector(
        const std::string& name
    ) = 0;

    /// Get all registered detectors
    [[nodiscard]] virtual std::vector<IDetector*> getAllDetectors() = 0;

    /// Enable or disable a detector
    virtual void setDetectorEnabled(
        const std::string& name,
        bool enabled
    ) = 0;

    // ── Integration Points ──────────────────────────────────────────────

    /// Called when a player connects
    virtual void onPlayerConnect(const PlayerIdentity& identity) = 0;

    /// Called when a player disconnects
    virtual void onPlayerDisconnect(const std::string& playerKey) = 0;

    /// Called when a player sends a command
    virtual void onPlayerCommand(
        const std::string& playerKey,
        const std::string& command
    ) = 0;

    /// Called when a player damages another player
    virtual void onPlayerDamage(
        const std::string& attackerKey,
        const std::string& victimKey,
        uint32_t damage,
        const std::string& weapon
    ) = 0;

    /// Called when a player kills another player
    virtual void onPlayerKill(
        const std::string& killerKey,
        const std::string& victimKey,
        const std::string& weapon,
        bool headshot
    ) = 0;

    /// Called when a player's user command is processed
    virtual void onPlayerCommand(
        const std::string& playerKey,
        const UserCmdData& cmd
    ) = 0;

    /// Called when a round starts
    virtual void onRoundStart() = 0;

    /// Called when a round ends
    virtual void onRoundEnd(uint8_t winnerTeam) = 0;

    /// Called on map change
    virtual void onMapChange(const std::string& mapName) = 0;

    /// Get engine status
    [[nodiscard]] virtual EngineStatus getStatus() const noexcept = 0;

    /// Execute a console command
    virtual bool executeCommand(const std::string& cmd,
                                 const std::string& args) = 0;

    /// Get singleton instance
    [[nodiscard]] static IAntiCheatEngine* instance() noexcept;
};

} // namespace sentinel
