// ============================================================================
// Sentinel AntiCheat - Risk Engine Unit Tests
// ============================================================================

#include "risk_engine/RiskEngine.h"
#include "logging/JsonLogger.h"
#include <cassert>
#include <iostream>

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name, expr) do { \
    if (!(expr)) { \
        std::cerr << "FAIL: " << name << std::endl; \
        testsFailed++; \
    } else { \
        testsPassed++; \
    } \
} while(0)

void test_risk_scoring() {
    // Create logger and risk engine
    sentinel::logging::JsonLogger logger;
    sentinel::LoggerConfig logCfg;
    logCfg.output = sentinel::LogOutput::Console;
    logCfg.consoleLevel = sentinel::LogLevel::Error;
    logger.initialize(logCfg);

    sentinel::risk::RiskEngine riskEngine(&logger);

    // Test initial risk is safe
    auto risk = riskEngine.getPlayerRisk("STEAM_0:1:12345");
    TEST("Initial risk is zero", risk.totalScore == 0);

    // Test detection processing
    sentinel::DetectionEvent event;
    event.type = sentinel::DetectionType::Aimbot;
    event.score = 25;
    event.description = "Snap aim detected";
    event.moduleName = "AimbotDetector";
    event.playerKey = "STEAM_0:1:12345";
    event.confidence = 0.8f;
    event.timestamp = std::chrono::system_clock::now();

    risk = riskEngine.processDetection("STEAM_0:1:12345", event);
    TEST("After detection, score increases", risk.totalScore >= 25);
    TEST("After detection, aim score updated", risk.aimScore >= 25);
    TEST("After detection, level is suspicious or higher",
         risk.level != sentinel::RiskLevel::Safe);

    // Test multiple detections accumulate
    event.score = 30;
    event.type = sentinel::DetectionType::SilentAim;
    risk = riskEngine.processDetection("STEAM_0:1:12345", event);
    TEST("Multiple detections accumulate", risk.totalScore >= 55);
}

void test_action_determination() {
    sentinel::logging::JsonLogger logger;
    sentinel::LoggerConfig logCfg;
    logCfg.output = sentinel::LogOutput::Console;
    logCfg.consoleLevel = sentinel::LogLevel::Error;
    logger.initialize(logCfg);

    sentinel::risk::RiskEngine riskEngine(&logger);

    sentinel::RiskScore score;
    score.totalScore = 10;
    auto action = riskEngine.determineAction(score);
    TEST("Low score: no action", action == sentinel::ActionType::None);

    score.totalScore = 20;
    action = riskEngine.determineAction(score);
    TEST("20 score: log action", action == sentinel::ActionType::Log);

    score.totalScore = 80;
    action = riskEngine.determineAction(score);
    TEST("80 score: kick threshold", action >= sentinel::ActionType::Kick ||
         action == sentinel::ActionType::RecordDemo);
}

void test_player_reset() {
    sentinel::logging::JsonLogger logger;
    sentinel::LoggerConfig logCfg;
    logCfg.output = sentinel::LogOutput::Console;
    logCfg.consoleLevel = sentinel::LogLevel::Error;
    logger.initialize(logCfg);

    sentinel::risk::RiskEngine riskEngine(&logger);

    // Add detection
    sentinel::DetectionEvent event;
    event.type = sentinel::DetectionType::SpeedHack;
    event.score = 30;
    event.playerKey = "STEAM_0:1:12345";
    event.timestamp = std::chrono::system_clock::now();

    riskEngine.processDetection("STEAM_0:1:12345", event);

    auto risk = riskEngine.getPlayerRisk("STEAM_0:1:12345");
    TEST("Player has risk after detection", risk.totalScore > 0);

    riskEngine.resetPlayer("STEAM_0:1:12345");
    risk = riskEngine.getPlayerRisk("STEAM_0:1:12345");
    TEST("After reset, risk is zero", risk.totalScore == 0);
}

int main() {
    std::cout << "=== Sentinel AntiCheat - Risk Engine Unit Tests ===" << std::endl;

    test_risk_scoring();
    test_action_determination();
    test_player_reset();

    std::cout << "Tests: " << (testsPassed + testsFailed)
              << " | Passed: " << testsPassed
              << " | Failed: " << testsFailed << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
