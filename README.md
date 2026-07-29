# Sentinel AntiCheat

**Professional server-side AntiCheat for Counter-Strike 1.6**

Sentinel AntiCheat is a completely server-side anti-cheat system that detects cheating behavior entirely from server data — no client installation, no DLL injection, no kernel driver, no launcher required.

## Features

- **100% Server-Side** — No client modifications whatsoever
- **Modular Architecture** — Independent detector modules with SOLID design
- **Accumulated Evidence** — No single-event bans; combines multiple detectors
- **Risk Engine** — Sophisticated scoring with time-based decay
- **Multiple Detection Categories** — Aim, Movement, Network, Behavior analysis
- **SQLite Database** — Persistent storage with abstracted database layer
- **JSON Logging** — Structured logs with daily rotation
- **REST API** — Admin endpoints for full remote management
- **Web Dashboard** — Modern dark-themed admin interface
- **AMX Mod X Compatible** — Native functions for AMXX plugins
- **Cross-Platform** — Linux and Windows support

## Architecture

```
src/
├── core/           # Core interfaces and engine
│   ├── Types.h           # Fundamental type definitions
│   ├── IDetector.h       # Detector interface (all modules implement this)
│   ├── IModule.h         # Module lifecycle interface
│   ├── IRiskEngine.h     # Risk scoring and action engine
│   ├── IPlayerManager.h  # Player data management
│   ├── IDatabase.h       # Abstract persistence layer
│   ├── ILogger.h         # Structured logging
│   ├── IConfig.h         # Configuration management
│   ├── IApi.h           # REST API interface
│   ├── IAntiCheatEngine.h # Main engine orchestrator
│   ├── CoreEngine.h      # Engine implementation header
│   └── CoreEngine.cpp    # Engine implementation
├── risk_engine/     # Risk scoring and action management
├── player/          # Player context management
├── modules/
│   ├── aim/         # Aimbot, Silent Aim, TriggerBot detection
│   ├── movement/    # BHop, SpeedHack, AutoStrafe detection
│   ├── network/     # FakeLag, packet manipulation detection
│   └── behavior/    # Consistency, pattern analysis
├── logging/         # JSON logger with rotation
├── database/        # SQLite implementation
├── config/          # YAML/JSON configuration
├── api/             # REST API server
├── rehlds/          # ReHLDS/Metamod plugin entry point
├── ammx/            # AMX Mod X compatibility layer
└── utils/           # Utility functions
config/              # Default configuration files
web/                 # Admin web dashboard
tests/               # Unit and integration tests
docs/                # Documentation and diagrams
```

## Detection Modules

### Aim Analysis
- Snap aim detection (instantaneous large angle changes)
- Silent aim detection (angle changes without animation)
- Perfect tracking analysis (unnaturally smooth target tracking)
- Trigger bot detection (sub-100ms reaction times)
- Micro-correction analysis (robotic adjustment patterns)
- Recoil compensation analysis
- Smoothing anomaly detection

### Movement Analysis
- BunnyHop script detection (>80% perfect hop ratio)
- Speed hack detection (exceeding game limits)
- Auto-strafe detection (perfect strafe synchronization)
- Ground strafe anomalies
- Duck script detection (perfectly consistent duck timing)
- Air movement analysis

### Network Analysis
- FakeLag detection (intentional choke manipulation)
- Command flood detection
- Packet manipulation detection
- UserCmd anomaly detection
- Tick irregularity detection

### Behavior Analysis
- Cross-round consistency analysis
- Repeated pattern detection
- Kill streak anomaly detection
- Headshot ratio analysis
- Statistical profile analysis

## Risk System

Every detector returns a score based on confidence:

| Risk Level | Score Range | Category |
|-----------|-------------|----------|
| Safe | 0-39 | Normal play |
| Suspicious | 40-69 | Needs observation |
| Highly Suspicious | 70-99 | Close monitoring |
| Cheating | 100-149 | Intervention required |
| Banned | 150+ | Automatic action |

### Actions (configurable)
- Log
- Notify Admin
- Record Demo
- Kick
- Temporary Ban
- Permanent Ban

## Build Instructions

### Prerequisites
- C++17/20 compatible compiler (GCC 9+, Clang 10+, MSVC 2019+)
- CMake 3.16+
- SQLite3
- yaml-cpp (optional, bundled fallback)
- nlohmann/json (optional, bundled fallback)
- cpp-httplib (optional, bundled fallback)

### Build

```bash
# Clone the repository
git clone https://github.com/yourorg/sentinel-anticheat.git
cd sentinel-anticheat

# Create build directory
mkdir build && cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . -j$(nproc)

# Run tests
ctest
```

### Platform-Specific

**Linux:**
```bash
sudo apt-get install build-essential cmake libsqlite3-dev libyaml-cpp-dev
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

**Windows (MSVC):**
```cmd
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

**Windows (MinGW):**
```bash
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
mingw32-make -j$(nproc)
```

## Installation (ReHLDS Server)

1. Build the project to produce `sentinel_anticheat.so` (Linux) or `sentinel_anticheat.dll` (Windows)
2. Copy the binary to your server's `addons/sentinel/` directory
3. Create `addons/sentinel/config.yaml` from `config/sentinel.yaml`
4. Add to `metamod/plugins.ini`:
   ```
   linux addons/sentinel/sentinel_anticheat.so
   win32 addons/sentinel/sentinel_anticheat.dll
   ```
5. Restart your server

## API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/v1/health` | System health check |
| GET | `/api/v1/dashboard` | Dashboard summary data |
| GET | `/api/v1/players` | List all players |
| GET | `/api/v1/players/{id}` | Player details |
| GET | `/api/v1/detections` | Detection events |
| GET | `/api/v1/logs` | System logs |
| GET | `/api/v1/statistics` | Full statistics |
| GET | `/api/v1/risk/leaderboard` | Risk score ranking |
| GET | `/api/v1/bans` | Active bans |
| POST | `/api/v1/bans` | Issue a ban |
| POST | `/api/v1/bans/unban` | Remove a ban |
| POST | `/api/v1/actions/kick` | Kick a player |
| POST | `/api/v1/config/reload` | Reload configuration |

## Configuration

All settings are in `config/sentinel.yaml`:

- Enable/disable individual detectors
- Configure risk thresholds per detection type
- Set score weights for each detection sub-type
- Configure auto-action thresholds
- Database connection settings
- API settings (port, auth token, CORS)
- Logging verbosity and rotation

## Detection Philosophy

Sentinel is built on the principle of **accumulated evidence**. No single event triggers a ban. Instead:

1. Each detector outputs a confidence score (0.0 - 1.0)
2. Scores are combined across detectors
3. Risk decays over time without continued violations
4. Actions escalate progressively (log → notify → kick → temp ban)
5. All thresholds are configurable

This design ensures legitimate skilled players are not flagged while cheaters accumulate irrefutable evidence.

## Future Features

- Plugin SDK for custom detectors
- Lua scripting support
- Discord/Telegram notifications
- WebSocket live events
- Machine Learning-based detection
- Replay analysis
- Heatmap generation
- Multi-server synchronization

## License

MIT License - See LICENSE file for details.

## Contributors

- **ZeLoExE** — Creator and maintainer

## Credits

Built with modern C++20, designed for the Half-Life GoldSrc community.
