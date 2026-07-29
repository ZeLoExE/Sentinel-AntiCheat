// ============================================================================
// Sentinel AntiCheat - Type Definitions Unit Tests
// ============================================================================

#include "core/Types.h"
#include <cassert>
#include <iostream>

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name, expr) do { \
    if (!(expr)) { \
        std::cerr << "FAIL: " << name << " (" << #expr << ")" << std::endl; \
        testsFailed++; \
    } else { \
        testsPassed++; \
    } \
} while(0)

void test_vector3() {
    sentinel::Vector3 v1(1.0f, 2.0f, 3.0f);
    sentinel::Vector3 v2(4.0f, 5.0f, 6.0f);

    TEST("Vector3 subtraction", (v2 - v1).x == 3.0f && (v2 - v1).y == 3.0f);
    TEST("Vector3 addition", (v1 + v2).x == 5.0f);
    TEST("Vector3 scalar multiply", (v1 * 2.0f).x == 2.0f);
    TEST("Vector3 length", v1.length() > 0.0f);
    TEST("Vector3 length2d", v1.length2d() > 0.0f);
    TEST("Vector3 dot", v1.dot(v2) == 32.0f);
    TEST("Vector3 normalize", v1.normalize().length() - 1.0f < 0.001f);
}

void test_risk_score() {
    sentinel::RiskScore score;
    TEST("RiskScore initial safe", score.level == sentinel::RiskLevel::Safe);
    TEST("RiskScore initial zero", score.totalScore == 0);

    score.aimScore = 25;
    score.movementScore = 15;
    score.update();

    TEST("RiskScore total", score.totalScore == 40);
    TEST("RiskScore suspicious", score.level == sentinel::RiskLevel::HighlySuspicious);

    score.aimScore = 100;
    score.update();
    TEST("RiskScore banned", score.level == sentinel::RiskLevel::Banned);
}

void test_steam_id() {
    sentinel::SteamId sid;
    TEST("SteamId starts invalid", !sid.valid());

    sid.accountId = 12345;
    sid.communityId = "STEAM_0:1:12345";
    TEST("SteamId becomes valid", sid.valid());
}

void test_player_identity() {
    sentinel::PlayerIdentity identity;
    identity.name = "TestPlayer";
    identity.ipAddress = "192.168.1.1";

    // Key should use steam ID if available, IP otherwise
    TEST("Identity key uses IP when no SteamID", identity.key() == "192.168.1.1");

    identity.steamId.communityId = "STEAM_0:1:12345";
    identity.steamId.accountId = 12345;
    TEST("Identity key uses SteamID when available", identity.key() == "STEAM_0:1:12345");
}

int main() {
    std::cout << "=== Sentinel AntiCheat - Types Unit Tests ===" << std::endl;

    test_vector3();
    test_risk_score();
    test_steam_id();
    test_player_identity();

    std::cout << "Tests: " << (testsPassed + testsFailed)
              << " | Passed: " << testsPassed
              << " | Failed: " << testsFailed << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
