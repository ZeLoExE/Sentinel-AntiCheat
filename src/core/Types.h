// ============================================================================
// Sentinel AntiCheat - Core Type Definitions
// ============================================================================
// Fundamental types, enums, and structures used throughout the system.
// ============================================================================

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <variant>
#include <optional>

namespace sentinel {

// ── Version ───────────────────────────────────────────────────────────────
inline constexpr const char* VERSION = "1.0.0";
inline constexpr const char* PROJECT_NAME = "Sentinel AntiCheat";

// ── Risk Level Enumeration ───────────────────────────────────────────────
enum class RiskLevel : uint8_t {
    Safe              = 0,
    Suspicious        = 1,
    HighlySuspicious  = 2,
    Cheating          = 3,
    Banned            = 4
};

// ── Detection Type ───────────────────────────────────────────────────────
enum class DetectionType : uint16_t {
    // Aim Detections
    Aimbot               = 0x0001,
    SilentAim            = 0x0002,
    PerfectTracking      = 0x0004,
    TriggerBot           = 0x0008,
    AimSmoothingAnomaly  = 0x0010,
    MicroCorrection      = 0x0020,
    RecoilCompensation   = 0x0040,

    // Movement Detections
    BHopScript           = 0x0100,
    SpeedHack            = 0x0200,
    AutoStrafe           = 0x0400,
    GroundStrafe         = 0x0800,
    DuckScript           = 0x1000,

    // Network Detections
    FakeLag              = 0x2000,
    PacketManipulation   = 0x4000,
    CommandFlood         = 0x8000,
};

// ── Action Type ──────────────────────────────────────────────────────────
enum class ActionType : uint8_t {
    None          = 0,
    Log           = 1,
    NotifyAdmin   = 2,
    RecordDemo    = 3,
    Kick          = 4,
    TempBan       = 5,
    PermBan       = 6
};

// ── Steam ID ─────────────────────────────────────────────────────────────
struct SteamId {
    uint64_t          accountId = 0;
    std::string       communityId;   // STEAM_0:x:xxxxx or STEAM_1:x:xxxxx
    std::string       steamId3;      // [U:1:xxxxx]
    std::string       steamId64;     // 7656119xxxxxxxxxx

    [[nodiscard]] bool valid() const noexcept { return accountId != 0; }
};

// ── Player Identification ────────────────────────────────────────────────
struct PlayerIdentity {
    SteamId           steamId;
    std::string       name;
    std::string       ipAddress;
    uint16_t          userId = 0;
    uint8_t           index  = 0;

    [[nodiscard]] std::string key() const {
        return steamId.valid() ? steamId.communityId : ipAddress;
    }
};

// ── Vector Math ──────────────────────────────────────────────────────────
struct Vector3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vector3() noexcept = default;
    Vector3(float x_, float y_, float z_) noexcept : x(x_), y(y_), z(z_) {}

    [[nodiscard]] Vector3 operator-(const Vector3& o) const noexcept {
        return {x - o.x, y - o.y, z - o.z};
    }
    [[nodiscard]] Vector3 operator+(const Vector3& o) const noexcept {
        return {x + o.x, y + o.y, z + o.z};
    }
    [[nodiscard]] Vector3 operator*(float s) const noexcept {
        return {x * s, y * s, z * s};
    }
    [[nodiscard]] float length() const noexcept {
        return std::sqrt(x * x + y * y + z * z);
    }
    [[nodiscard]] float length2d() const noexcept {
        return std::sqrt(x * x + y * y);
    }
    [[nodiscard]] float dot(const Vector3& o) const noexcept {
        return x * o.x + y * o.y + z * o.z;
    }
    [[nodiscard]] Vector3 normalize() const noexcept {
        float len = length();
        return (len > 0.0001f) ? Vector3{x / len, y / len, z / len} : Vector3{};
    }
};

// ── Angle Representation ─────────────────────────────────────────────────
struct QAngle {
    float pitch = 0.0f;  // x
    float yaw   = 0.0f;  // y
    float roll  = 0.0f;  // z

    QAngle() noexcept = default;
    QAngle(float p, float y, float r) noexcept : pitch(p), yaw(y), roll(r) {}

    [[nodiscard]] QAngle operator-(const QAngle& o) const noexcept {
        return {pitch - o.pitch, yaw - o.yaw, roll - o.roll};
    }
    [[nodiscard]] QAngle operator+(const QAngle& o) const noexcept {
        return {pitch + o.pitch, yaw + o.yaw, roll + o.roll};
    }
    [[nodiscard]] float length() const noexcept {
        return std::sqrt(pitch * pitch + yaw * yaw + roll * roll);
    }
};

// ── User Command Data ────────────────────────────────────────────────────
struct UserCmdData {
    uint32_t    buttons = 0;
    QAngle      viewAngles;
    float       forwardMove = 0.0f;
    float       sideMove    = 0.0f;
    float       upMove      = 0.0f;
    uint8_t     impulse     = 0;
    uint16_t    msec        = 0;
    uint32_t    weaponSelect = 0;
    uint8_t     weaponSubType = 0;
    int16_t     randomSeed   = 0;
    uint32_t    tickCount   = 0;
};

// ── Player Stats Snapshot ────────────────────────────────────────────────
struct PlayerStats {
    uint32_t kills           = 0;
    uint32_t deaths          = 0;
    uint32_t headshots       = 0;
    uint32_t shots           = 0;
    uint32_t hits            = 0;
    uint32_t damage          = 0;
    float    accuracy        = 0.0f;
    float    headshotRatio   = 0.0f;
    float    avgReactionMs   = 0.0f;
    float    avgKillDistance = 0.0f;
    uint32_t killStreak      = 0;
    uint32_t bestKillStreak  = 0;
    uint32_t roundsPlayed    = 0;
    float    kdRatio         = 0.0f;
};

// ── Detection Event ──────────────────────────────────────────────────────
struct DetectionEvent {
    DetectionType       type;
    RiskLevel           severity;
    uint32_t            score;
    std::string         description;
    std::chrono::system_clock::time_point timestamp;
    std::string         playerKey;
    std::string         moduleName;
    float               confidence;  // 0.0 - 1.0
};

// ── Ban Record ───────────────────────────────────────────────────────────
struct BanRecord {
    std::string         playerKey;
    std::string         name;
    std::string         ipAddress;
    ActionType          banType;
    std::string         reason;
    std::chrono::system_clock::time_point issuedAt;
    std::chrono::system_clock::time_point expiresAt;
    std::string         issuedBy;  // "system" or admin name
    bool                active = true;
};

// ── Risk Score ───────────────────────────────────────────────────────────
struct RiskScore {
    uint32_t totalScore    = 0;
    uint32_t aimScore      = 0;
    uint32_t movementScore = 0;
    uint32_t networkScore  = 0;
    uint32_t behaviorScore = 0;
    uint32_t detectionCount = 0;
    RiskLevel level        = RiskLevel::Safe;

    void update() {
        totalScore = aimScore + movementScore + networkScore + behaviorScore;
        if (totalScore >= 100) level = RiskLevel::Banned;
        else if (totalScore >= 70) level = RiskLevel::Cheating;
        else if (totalScore >= 40) level = RiskLevel::HighlySuspicious;
        else if (totalScore >= 15) level = RiskLevel::Suspicious;
        else level = RiskLevel::Safe;
    }
};

// ── Callbacks ────────────────────────────────────────────────────────────
using DetectionCallback = std::function<void(const DetectionEvent&)>;
using BanCallback       = std::function<void(const BanRecord&)>;
using RiskCallback      = std::function<void(const std::string& playerKey, const RiskScore&)>;

// ── Configuration Value ─────────────────────────────────────────────────
using ConfigValue = std::variant<
    int64_t, double, bool, std::string,
    std::vector<int64_t>,
    std::vector<double>,
    std::vector<std::string>
>;

// ── Entity ID ────────────────────────────────────────────────────────────
using EntityIndex = uint16_t;
inline constexpr EntityIndex INVALID_ENTITY = 0xFFFF;

// ── Team Enum ────────────────────────────────────────────────────────────
enum class Team : uint8_t {
    Unassigned = 0,
    Terrorist  = 1,
    CounterTerrorist = 2,
    Spectator  = 3
};

// ── Weapon Class ─────────────────────────────────────────────────────────
enum class WeaponClass : uint8_t {
    None,
    Pistol,
    Shotgun,
    SMG,
    Rifle,
    Sniper,
    MachineGun,
    Grenade,
    Knife,
    Other
};

} // namespace sentinel
