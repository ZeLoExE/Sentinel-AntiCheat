// ============================================================================
// Sentinel AntiCheat - Core Engine Implementation
// ============================================================================
// The central orchestrator that wires all subsystems together.
// Implements the IAntiCheatEngine interface as a singleton.
// ============================================================================

#pragma once
#include "IAntiCheatEngine.h"
#include "Types.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>

namespace sentinel::core {

/// Core Engine Implementation
class CoreEngine final : public IAntiCheatEngine {
public:
    static CoreEngine& getInstance() {
        static CoreEngine instance;
        return instance;
    }

    // ── IModule Implementation ───────────────────────────────────────────
    const std::string& moduleName() const noexcept override;
    ModuleState state() const noexcept override;
    bool initialize() override;
    bool initialize(const EngineConfig& config) override;
    bool start() override;
    void pause() override;
    void resume() override;
    void shutdown() override;
    void update(float deltaTime) override;
    bool handleCommand(const std::string& cmd, const std::string& args) override;
    std::string status() const override;

    // ── Subsystem Access ─────────────────────────────────────────────────
    IRiskEngine*       riskEngine()       noexcept override;
    IPlayerManager*    playerManager()    noexcept override;
    IDatabase*         database()         noexcept override;
    ILogger*           logger()           noexcept override;
    IConfig*           configManager()    noexcept override;
    IApi*              api()              noexcept override;

    // ── Module Management ───────────────────────────────────────────────
    bool registerModule(ModulePtr module) override;
    IModule* getModule(const std::string& name) override;
    std::vector<IModule*> getAllModules() override;

    // ── Detector Management ─────────────────────────────────────────────
    bool registerDetector(DetectorPtr detector) override;
    IDetector* getDetector(const std::string& name) override;
    std::vector<IDetector*> getAllDetectors() override;
    void setDetectorEnabled(const std::string& name, bool enabled) override;

    // ── Integration Points ──────────────────────────────────────────────
    void onPlayerConnect(const PlayerIdentity& identity) override;
    void onPlayerDisconnect(const std::string& playerKey) override;
    void onPlayerCommand(const std::string& playerKey,
                          const std::string& command) override;
    void onPlayerDamage(const std::string& attackerKey,
                         const std::string& victimKey,
                         uint32_t damage,
                         const std::string& weapon) override;
    void onPlayerKill(const std::string& killerKey,
                       const std::string& victimKey,
                       const std::string& weapon,
                       bool headshot) override;
    void onPlayerCommand(const std::string& playerKey,
                          const UserCmdData& cmd) override;
    void onRoundStart() override;
    void onRoundEnd(uint8_t winnerTeam) override;
    void onMapChange(const std::string& mapName) override;

    // ── Engine Status ───────────────────────────────────────────────────
    EngineStatus getStatus() const noexcept override;
    bool executeCommand(const std::string& cmd,
                         const std::string& args) override;

private:
    CoreEngine() = default;
    ~CoreEngine() override;
    CoreEngine(const CoreEngine&) = delete;
    CoreEngine& operator=(const CoreEngine&) = delete;

    bool initializeSubsystems();
    bool loadDetectors();
    bool loadModules();
    void runDetectorUpdates(float deltaTime);

    // Subsystems
    std::unique_ptr<IRiskEngine>    m_riskEngine;
    std::unique_ptr<IPlayerManager> m_playerManager;
    std::unique_ptr<IDatabase>      m_database;
    std::unique_ptr<ILogger>        m_logger;
    std::unique_ptr<IConfig>        m_config;
    std::unique_ptr<IApi>           m_api;

    // Modules and Detectors
    std::vector<ModulePtr>          m_modules;
    std::vector<DetectorPtr>        m_detectors;
    std::unordered_map<std::string, IDetector*> m_detectorMap;

    // State
    std::atomic<ModuleState>        m_state{ModuleState::Unloaded};
    EngineConfig                    m_engineConfig;
    EngineStatus                    m_status;
    std::chrono::steady_clock::time_point m_startTime;

    // Thread Safety
    mutable std::recursive_mutex    m_mutex;
    uint32_t                        m_frameCount = 0;
};

} // namespace sentinel::core
