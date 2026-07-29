// ============================================================================
// Sentinel AntiCheat - Module Interface
// ============================================================================
// Each major subsystem implements this lifecycle interface.
// Modules are loaded, initialized, and torn down by the Core Engine.
// ============================================================================

#pragma once
#include <string>
#include <memory>

namespace sentinel {

/// Module lifecycle states
enum class ModuleState : uint8_t {
    Unloaded    = 0,
    Loaded      = 1,
    Initialized = 2,
    Running     = 3,
    Paused      = 4,
    Error       = 5,
    Destroyed   = 6
};

/// Module interface for lifecycle management
class IModule {
public:
    virtual ~IModule() = default;

    /// Unique module name
    [[nodiscard]] virtual const std::string& moduleName() const noexcept = 0;

    /// Current lifecycle state
    [[nodiscard]] virtual ModuleState state() const noexcept = 0;

    /// Initialize the module (allocate resources, register callbacks)
    /// @return true if initialization succeeded
    virtual bool initialize() = 0;

    /// Start the module (begin processing)
    /// @return true if start succeeded
    virtual bool start() = 0;

    /// Pause the module (stop processing but keep resources)
    virtual void pause() = 0;

    /// Resume from paused state
    virtual void resume() = 0;

    /// Shutdown and release all resources
    virtual void shutdown() = 0;

    /// Called every server frame
    /// @param deltaTime Seconds since last frame
    virtual void update(float deltaTime) = 0;

    /// Handle a console command directed at this module
    /// @param args Space-separated command arguments
    /// @return true if the command was handled
    virtual bool handleCommand(const std::string& cmd, const std::string& args) = 0;

    /// Get module status info as a string (for status commands)
    [[nodiscard]] virtual std::string status() const = 0;
};

using ModulePtr = std::unique_ptr<IModule>;

} // namespace sentinel
