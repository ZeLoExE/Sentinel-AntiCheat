// ============================================================================
// Sentinel AntiCheat - Core Engine Implementation
// ============================================================================
// Central singleton that initializes, wires, and manages all subsystems.
// ============================================================================

#include "CoreEngine.h"
#include "risk_engine/RiskEngine.h"
#include "player/PlayerManager.h"
#include "database/SQLiteDatabase.h"
#include "logging/JsonLogger.h"
#include "config/YamlConfig.h"
#include "api/RestApi.h"
#include "modules/aim/AimbotDetector.h"
#include "modules/movement/MovementDetector.h"
#include "modules/network/NetworkDetector.h"
#include "modules/behavior/BehaviorDetector.h"

namespace sentinel::core {

// ── Static Instance ─────────────────────────────────────────────────────
static CoreEngine* g_instance = nullptr;

IAntiCheatEngine* IAntiCheatEngine::instance() noexcept {
    return g_instance;
}

// ── Destructor ──────────────────────────────────────────────────────────
CoreEngine::~CoreEngine() {
    shutdown();
    g_instance = nullptr;
}

// ── IModule: name ───────────────────────────────────────────────────────
const std::string& CoreEngine::moduleName() const noexcept {
    static const std::string name = "CoreEngine";
    return name;
}

// ── IModule: state ──────────────────────────────────────────────────────
ModuleState CoreEngine::state() const noexcept {
    return m_state.load();
}

// ── IModule: initialize (default config) ────────────────────────────────
bool CoreEngine::initialize() {
    EngineConfig cfg;
    return initialize(cfg);
}

// ── IModule: initialize (with config) ───────────────────────────────────
bool CoreEngine::initialize(const EngineConfig& config) {
    std::lock_guard lock(m_mutex);

    m_engineConfig = config;
    m_state.store(ModuleState::Loaded);

    // Create subsystems
    m_logger = std::make_unique<logging::JsonLogger>();
    m_config = std::make_unique<config::YamlConfig>();
    m_riskEngine = std::make_unique<risk::RiskEngine>(m_logger.get());
    m_database = std::make_unique<database::SQLiteDatabase>(m_logger.get());
    // PlayerManager needs logger + database
    m_playerManager = std::make_unique<player::PlayerManager>(m_logger.get(), m_database.get());
    // API needs engine + logger
    m_api = std::make_unique<api::RestApi>(this, m_logger.get());

    if (!initializeSubsystems()) {
        m_state.store(ModuleState::Error);
        return false;
    }

    // Load detectors
    if (m_engineConfig.autoLoadModules) {
        loadDetectors();
    }

    // Registerself as a module
    if (m_engineConfig.autoLoadModules) {
        loadModules();
    }

    m_startTime = std::chrono::steady_clock::now();
    m_state.store(ModuleState::Initialized);
    g_instance = this;

    m_logger->info("CoreEngine", "Sentinel AntiCheat v" + std::string(VERSION) + " initialized successfully");
    return true;
}

// ── Initialize Subsystems ───────────────────────────────────────────────
bool CoreEngine::initializeSubsystems() {
    // Initialize logger
    LoggerConfig logCfg;
    logCfg.logDirectory = m_engineConfig.logPath;
    logCfg.debugMode = m_engineConfig.debugMode;
    if (!m_logger->initialize(logCfg)) {
        return false;
    }

    // Load configuration
    ConfigSource cfgSrc;
    cfgSrc.path = m_engineConfig.configPath;
    if (!m_config->load(cfgSrc)) {
        m_logger->warn("CoreEngine", "Failed to load config at " + m_engineConfig.configPath + ", using defaults");
        m_config->resetToDefaults();
    }

    // Initialize database
    auto dbConfig = DatabaseConfig{};
    dbConfig.path = m_engineConfig.dataPath + "sentinel.db";
    if (!m_database->connect(dbConfig) || !m_database->initializeSchema()) {
        m_logger->error("CoreEngine", "Failed to initialize database");
        return false;
    }

    // Start API if enabled
    if (m_engineConfig.enableApi) {
        ApiConfig apiCfg;
        m_api->start(apiCfg);
    }

    return true;
}

// ── Load Detectors ──────────────────────────────────────────────────────
bool CoreEngine::loadDetectors() {
    // Create and register aimbot detector
    auto aimDetector = std::make_unique<modules::aim::AimbotDetector>(
        m_playerManager.get(), m_logger.get()
    );
    auto* aimPtr = aimDetector.get();
    auto aimConfig = m_config->getDetectorConfig("detectors.aimbot");
    aimPtr->configure(aimConfig);
    m_detectors.push_back(std::move(aimDetector));
    m_detectorMap["AimbotDetector"] = aimPtr;

    // Create and register movement detector
    auto moveDetector = std::make_unique<modules::movement::MovementDetector>(
        m_playerManager.get(), m_logger.get()
    );
    auto* movePtr = moveDetector.get();
    auto moveConfig = m_config->getDetectorConfig("detectors.movement");
    movePtr->configure(moveConfig);
    m_detectors.push_back(std::move(moveDetector));
    m_detectorMap["MovementDetector"] = movePtr;

    // Create and register network detector
    auto netDetector = std::make_unique<modules::network::NetworkDetector>(
        m_playerManager.get(), m_logger.get()
    );
    auto* netPtr = netDetector.get();
    auto netConfig = m_config->getDetectorConfig("detectors.network");
    netPtr->configure(netConfig);
    m_detectors.push_back(std::move(netDetector));
    m_detectorMap["NetworkDetector"] = netPtr;

    // Create and register behavior detector
    auto behDetector = std::make_unique<modules::behavior::BehaviorDetector>(
        m_playerManager.get(), m_logger.get()
    );
    auto* behPtr = behDetector.get();
    auto behConfig = m_config->getDetectorConfig("detectors.behavior");
    behPtr->configure(behConfig);
    m_detectors.push_back(std::move(behDetector));
    m_detectorMap["BehaviorDetector"] = behPtr;

    m_logger->info("CoreEngine", "Loaded " + std::to_string(m_detectors.size()) + " detection modules");
    return true;
}

// ── Load Modules ────────────────────────────────────────────────────────
bool CoreEngine::loadModules() {
    // Register self
    // (Modules would be registered here)
    m_status.activeModules = static_cast<uint32_t>(m_modules.size() + m_detectors.size());
    return true;
}

// ── IModule: start ──────────────────────────────────────────────────────
bool CoreEngine::start() {
    std::lock_guard lock(m_mutex);
    m_state.store(ModuleState::Running);
    m_logger->info("CoreEngine", "Sentinel AntiCheat started");
    return true;
}

// ── IModule: pause ──────────────────────────────────────────────────────
void CoreEngine::pause() {
    m_state.store(ModuleState::Paused);
    m_logger->info("CoreEngine", "Sentinel AntiCheat paused");
}

// ── IModule: resume ─────────────────────────────────────────────────────
void CoreEngine::resume() {
    m_state.store(ModuleState::Running);
    m_logger->info("CoreEngine", "Sentinel AntiCheat resumed");
}

// ── IModule: shutdown ───────────────────────────────────────────────────
void CoreEngine::shutdown() {
    std::lock_guard lock(m_mutex);

    if (m_state.load() == ModuleState::Destroyed) return;

    m_state.store(ModuleState::Destroyed);
    m_logger->info("CoreEngine", "Sentinel AntiCheat shutting down...");

    // Stop subsystems in reverse order
    if (m_api) m_api->stop();

    m_detectors.clear();
    m_detectorMap.clear();
    m_modules.clear();

    if (m_database) m_database->disconnect();
    if (m_logger) m_logger->shutdown();
}

// ── IModule: update ─────────────────────────────────────────────────────
void CoreEngine::update(float deltaTime) {
    if (m_state.load() != ModuleState::Running) return;

    m_frameCount++;
    runDetectorUpdates(deltaTime);

    // Update risk decay
    if (m_riskEngine) {
        auto* re = dynamic_cast<risk::RiskEngine*>(m_riskEngine.get());
        if (re) re->updateDecay(deltaTime);
    }

    // Update all modules
    for (auto& mod : m_modules) {
        if (mod->state() == ModuleState::Running) {
            mod->update(deltaTime);
        }
    }
}

// ── Run Detector Updates ─────────────────────────────────────────────────
void CoreEngine::runDetectorUpdates(float deltaTime) {
    auto players = m_playerManager->getAllPlayers();
    for (auto* player : players) {
        for (auto& detector : m_detectors) {
            if (!detector->config().enabled) continue;

            auto result = detector->detect(player->identity.key(), deltaTime);
            if (result.detected) {
                // Create detection event
                DetectionEvent event;
                event.type = result.type;
                event.score = result.score;
                event.description = result.evidence;
                event.moduleName = result.moduleName;
                event.playerKey = player->identity.key();
                event.timestamp = std::chrono::system_clock::now();
                event.confidence = result.confidence;

                // Determine severity based on score
                if (result.score >= 100) event.severity = RiskLevel::Banned;
                else if (result.score >= 70) event.severity = RiskLevel::Cheating;
                else if (result.score >= 40) event.severity = RiskLevel::HighlySuspicious;
                else if (result.score >= 15) event.severity = RiskLevel::Suspicious;
                else event.severity = RiskLevel::Safe;

                // Process through risk engine
                m_riskEngine->processDetection(player->identity.key(), event);
                m_status.detectionsTotal++;

                // Log the detection
                m_logger->logDetection(event);

                // Add to player history
                m_playerManager->addDetection(player->identity.key(), event);
            }
        }
    }
}

// ── IModule: handleCommand ──────────────────────────────────────────────
bool CoreEngine::handleCommand(const std::string& cmd, const std::string& args) {
    if (cmd == "sentinel_status" || cmd == "ac_status") {
        m_logger->info("CoreEngine", status());
        return true;
    }
    if (cmd == "sentinel_pause") {
        pause();
        return true;
    }
    if (cmd == "sentinel_resume") {
        resume();
        return true;
    }
    if (cmd == "sentinel_reload") {
        m_config->reload();
        m_logger->info("CoreEngine", "Configuration reloaded");
        return true;
    }
    return false;
}

// ── IModule: status ─────────────────────────────────────────────────────
std::string CoreEngine::status() const {
    auto s = getStatus();
    std::string result;
    result += "=== Sentinel AntiCheat v" + s.version + " ===\n";
    result += "Status: " + (s.running ? "Running" : "Stopped") + "\n";
    result += "Uptime: " + std::to_string(s.uptimeSec) + "s\n";
    result += "Players: " + std::to_string(s.playersTracked) + "\n";
    result += "Detections: " + std::to_string(s.detectionsTotal) + "\n";
    result += "Bans: " + std::to_string(s.bansIssued) + "\n";
    result += "Modules: " + std::to_string(s.activeModules) + "\n";
    return result;
}

// ── Subsystem Access ────────────────────────────────────────────────────
IRiskEngine*    CoreEngine::riskEngine()       noexcept { return m_riskEngine.get(); }
IPlayerManager* CoreEngine::playerManager()    noexcept { return m_playerManager.get(); }
IDatabase*      CoreEngine::database()         noexcept { return m_database.get(); }
ILogger*        CoreEngine::logger()           noexcept { return m_logger.get(); }
IConfig*        CoreEngine::configManager()    noexcept { return m_config.get(); }
IApi*           CoreEngine::api()              noexcept { return m_api.get(); }

// ── Module Management ───────────────────────────────────────────────────
bool CoreEngine::registerModule(ModulePtr module) {
    if (!module) return false;
    std::lock_guard lock(m_mutex);
    m_modules.push_back(std::move(module));
    m_status.activeModules++;
    return true;
}

IModule* CoreEngine::getModule(const std::string& name) {
    for (auto& mod : m_modules) {
        if (mod->moduleName() == name) return mod.get();
    }
    return nullptr;
}

std::vector<IModule*> CoreEngine::getAllModules() {
    std::vector<IModule*> result;
    for (auto& mod : m_modules) result.push_back(mod.get());
    return result;
}

// ── Detector Management ─────────────────────────────────────────────────
bool CoreEngine::registerDetector(DetectorPtr detector) {
    if (!detector) return false;
    std::lock_guard lock(m_mutex);
    auto* ptr = detector.get();
    m_detectors.push_back(std::move(detector));
    m_detectorMap[ptr->name()] = ptr;
    m_status.activeModules++;
    return true;
}

IDetector* CoreEngine::getDetector(const std::string& name) {
    auto it = m_detectorMap.find(name);
    return (it != m_detectorMap.end()) ? it->second : nullptr;
}

std::vector<IDetector*> CoreEngine::getAllDetectors() {
    std::vector<IDetector*> result;
    for (auto& det : m_detectors) result.push_back(det.get());
    return result;
}

void CoreEngine::setDetectorEnabled(const std::string& name, bool enabled) {
    auto* det = getDetector(name);
    if (det) {
        auto cfg = det->config();
        cfg.enabled = enabled;
        det->configure(cfg);
        m_logger->info("CoreEngine", "Detector '" + name + "' " + (enabled ? "enabled" : "disabled"));
    }
}

// ── Integration Points ──────────────────────────────────────────────────
void CoreEngine::onPlayerConnect(const PlayerIdentity& identity) {
    m_playerManager->registerPlayer(identity);
    m_status.playersTracked = static_cast<uint32_t>(m_playerManager->playerCount());
    m_logger->info("CoreEngine", "Player connected: " + identity.name + " [" + identity.steamId.communityId + "]", identity.key());
}

void CoreEngine::onPlayerDisconnect(const std::string& playerKey) {
    m_playerManager->removePlayer(playerKey);
    m_status.playersTracked = static_cast<uint32_t>(m_playerManager->playerCount());
    m_logger->info("CoreEngine", "Player disconnected: " + playerKey);
}

void CoreEngine::onPlayerCommand(const std::string& playerKey, const std::string& command) {
    // Log admin commands
    if (command.find("ban") != std::string::npos || command.find("kick") != std::string::npos) {
        m_logger->info("CoreEngine", "Admin command from " + playerKey + ": " + command);
    }
}

void CoreEngine::onPlayerDamage(const std::string& attackerKey, const std::string& victimKey,
                                  uint32_t damage, const std::string& weapon) {
    // Update player stats
    auto* attacker = m_playerManager->getPlayer(attackerKey);
    if (attacker) {
        attacker->stats.damage += damage;
        attacker->stats.hits++;
    }
}

void CoreEngine::onPlayerKill(const std::string& killerKey, const std::string& victimKey,
                                const std::string& weapon, bool headshot) {
    auto* killer = m_playerManager->getPlayer(killerKey);
    auto* victim = m_playerManager->getPlayer(victimKey);

    if (killer) {
        killer->stats.kills++;
        killer->stats.killStreak++;
        if (headshot) killer->stats.headshots++;
        if (killer->stats.killStreak > killer->stats.bestKillStreak) {
            killer->stats.bestKillStreak = killer->stats.killStreak;
        }
    }
    if (victim) {
        victim->stats.deaths++;
        victim->stats.killStreak = 0;
    }
}

void CoreEngine::onPlayerCommand(const std::string& playerKey, const UserCmdData& cmd) {
    // Feed to relevant detectors
    // (Detectors access this via their record* methods)
}

void CoreEngine::onRoundStart() {
    m_logger->info("CoreEngine", "Round started");
}

void CoreEngine::onRoundEnd(uint8_t winnerTeam) {
    // Update round-based stats for all players
    auto players = m_playerManager->getAllPlayers();
    for (auto* p : players) {
        p->stats.roundsPlayed++;
    }
}

void CoreEngine::onMapChange(const std::string& mapName) {
    m_logger->info("CoreEngine", "Map changed to: " + mapName);
    // Reset per-map data
    for (auto& det : m_detectors) {
        det->resetAll();
    }
}

// ── Engine Status ───────────────────────────────────────────────────────
EngineStatus CoreEngine::getStatus() const noexcept {
    EngineStatus s;
    s.initialized = (m_state.load() >= ModuleState::Initialized);
    s.running = (m_state.load() == ModuleState::Running);
    s.playersTracked = m_status.playersTracked;
    s.detectionsTotal = m_status.detectionsTotal;
    s.bansIssued = m_status.bansIssued;
    s.activeModules = m_status.activeModules;
    s.version = VERSION;
    s.platform =
#ifdef SENTINEL_WINDOWS
        "Windows";
#else
        "Linux";
#endif

    if (m_state.load() >= ModuleState::Initialized) {
        auto now = std::chrono::steady_clock::now();
        s.uptimeSec = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                now - m_startTime
            ).count()
        );
    }

    return s;
}

// ── Execute Command ─────────────────────────────────────────────────────
bool CoreEngine::executeCommand(const std::string& cmd, const std::string& args) {
    return handleCommand(cmd, args);
}

} // namespace sentinel::core
