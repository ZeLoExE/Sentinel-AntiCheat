// ============================================================================
// Sentinel AntiCheat - REST API Interface
// ============================================================================
// Admin REST API for remote management, monitoring, and integration.
// Provides JSON endpoints for all anti-cheat data.
// ============================================================================

#pragma once
#include "Types.h"
#include <string>
#include <functional>

namespace sentinel {

/// API server configuration
struct ApiConfig {
    bool        enabled          = true;
    std::string host             = "127.0.0.1";
    uint16_t    port             = 8080;
    std::string authToken        = "change-me-sentinel-admin-token";
    bool        requireAuth      = true;
    bool        enableCors       = true;
    uint32_t    maxConnections   = 100;
    bool        httpsEnabled     = false;
    std::string sslCertPath;
    std::string sslKeyPath;
    uint32_t    rateLimit        = 100;  // Requests per minute
};

/// API Server Interface
class IApi {
public:
    virtual ~IApi() = default;

    /// Start the API server
    virtual bool start(const ApiConfig& config) = 0;

    /// Stop the API server
    virtual void stop() = 0;

    /// Check if server is running
    [[nodiscard]] virtual bool isRunning() const noexcept = 0;

    /// Register a custom endpoint
    virtual void registerEndpoint(
        const std::string& method,   // "GET", "POST", etc.
        const std::string& path,
        std::function<std::string(const std::string& body,
                                   const std::string& query)> handler
    ) = 0;

    /// Get API configuration
    [[nodiscard]] virtual const ApiConfig& config() const noexcept = 0;

    /// Update configuration at runtime
    virtual void configure(const ApiConfig& cfg) = 0;

    /// Broadcast an event to WebSocket clients (future)
    virtual void broadcastEvent(const std::string& eventType,
                                 const std::string& jsonData) = 0;
};

} // namespace sentinel
