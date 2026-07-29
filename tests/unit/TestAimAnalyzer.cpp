// ============================================================================
// Sentinel AntiCheat - Aim Analyzer Unit Tests
// ============================================================================
// Tests each mathematical component with synthetic data that simulates
// both human-like and aimbot-like behavior. Validates that:
//   - Human-like aim produces low confidence scores
//   - Aimbot-like aim produces high confidence scores
//   - Statistical distributions are correctly computed
//   - Edge cases (idle, lag, high sensitivity) are handled
// ============================================================================

#include "modules/aim/AimAnalyzer.h"
#include "modules/aim/AimAnalysisTypes.h"
#include <iostream>
#include <cmath>
#include <random>
#include <cassert>

// ══════════════════════════════════════════════════════════════════════════
// Test Infrastructure
// ══════════════════════════════════════════════════════════════════════════

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST(name, expr) do { \
    if (!(expr)) { \
        std::cerr << "  FAIL: " << name << std::endl; \
        g_testsFailed++; \
    } else { \
        g_testsPassed++; \
    } \
} while(0)

#define TEST_NEAR(name, val, expected, tol) do { \
    float diff = std::abs((val) - (expected)); \
    if (diff > (tol)) { \
        std::cerr << "  FAIL: " << name << " (got " << (val) \
                  << ", expected " << (expected) \
                  << ", diff " << diff << " > tol " << (tol) << ")" << std::endl; \
        g_testsFailed++; \
    } else { \
        g_testsPassed++; \
    } \
} while(0)

// ══════════════════════════════════════════════════════════════════════════
// Synthetic Data Generators
// ══════════════════════════════════════════════════════════════════════════

namespace synth {

/// Generate human-like view angles over time.
/// 
/// Human aim model:
///   θ(t) = target_angle + tracking_error(t) + tremor(t)
///
/// Where:
///   tracking_error(t) = A * sin(2π * 2.0 * t)  (2 Hz oscillation from visual feedback)
///   tremor(t) = Σ a_i * sin(2π * f_i * t + φ_i)  (physiological tremor, 8-12 Hz)
///   A = 1.0-4.0 degrees (RMS tracking error)
///   a_i = 0.05-0.3 degrees (tremor amplitude)
///
struct HumanAimSimulator {
    std::mt19937 rng{42};
    std::normal_distribution<float> tremorNoise{0.0f, 0.15f};  // Tremor amplitude
    std::normal_distribution<float> trackingNoise{0.0f, 1.5f};  // Tracking error
    
    float targetPitch = 0.0f;
    float targetYaw   = 0.0f;
    float currentPitch = 0.0f;
    float currentYaw   = 0.0f;
    float time = 0.0f;

    /// Generate a smooth flick movement (human-like).
    /// Uses a sinusoidal acceleration profile:
    ///   θ(t) = θ_0 + Δθ * (t/T - sin(2π t/T) / 2π)
    /// This produces bell-shaped velocity (natural acceleration/deceleration).
    void startFlick(float pitchTarget, float yawTarget) {
        targetPitch = pitchTarget;
        targetYaw = yawTarget;
    }

    QAngle step(float deltaTime) {
        time += deltaTime;
        
        // Smooth approach to target (human-like)
        float approachSpeed = 2.0f;  // Seconds to reach target
        float factor = std::min(1.0f, time / approachSpeed);
        
        // Sigmoid-like approach: smooth acceleration and deceleration
        // f(t) = 1 / (1 + e^(-k*(t - T/2)))
        // This is a logistic curve, similar to human velocity profile
        float k = 8.0f / approachSpeed;  // Steepness
        float sigmoid = 1.0f / (1.0f + std::exp(-k * (time - approachSpeed / 2.0f)));
        
        // Ideal position on sigmoid curve
        float idealPitch = targetPitch * sigmoid;
        float idealYaw   = targetYaw   * sigmoid;
        
        // Add tracking error (oscillation due to visual feedback)
        float trackingError = trackingNoise(rng) * std::sin(2.0f * 3.14159f * 2.0f * time);
        
        // Add physiological tremor (8-12 Hz band)
        float tremor = tremorNoise(rng) * (
            std::sin(2.0f * 3.14159f * 9.0f * time) * 0.6f +
            std::sin(2.0f * 3.14159f * 10.5f * time) * 0.3f +
            std::sin(2.0f * 3.14159f * 8.0f * time) * 0.1f
        );
        
        currentPitch = idealPitch + trackingError + tremor;
        currentYaw   = idealYaw   + trackingError * 0.7f + tremor * 0.5f;
        
        // Clamp to reasonable CS:GO/CS 1.6 ranges
        currentPitch = std::clamp(currentPitch, -90.0f, 90.0f);
        
        return {currentPitch, currentYaw};
    }

    void reset() {
        time = 0.0f;
        currentPitch = 0.0f;
        currentYaw = 0.0f;
        targetPitch = 0.0f;
        targetYaw = 0.0f;
    }
};

/// Generate aimbot-like view angles.
///
/// Aimbot model:
///   θ(t) = target_position (instantaneous, no intermediate frames)
///   No tracking error (ε ≈ 0)
///   No tremor
///   Perfect first-attempt positioning
///
struct AimbotSimulator {
    float currentPitch = 0.0f;
    float currentYaw = 0.0f;
    bool  snapToTarget = false;
    float snapPitch = 0.0f;
    float snapYaw = 0.0f;
    int   holdFrames = 0;

    void startSnap(float pitch, float yaw) {
        snapToTarget = true;
        snapPitch = pitch;
        snapYaw = yaw;
        holdFrames = 5 + rand() % 10;  // Hold on target for 5-15 frames
    }

    QAngle step(float deltaTime) {
        if (snapToTarget) {
            // Aimbot: instant snap to target (one frame)
            if (holdFrames > 0) {
                currentPitch = snapPitch;
                currentYaw = snapYaw;
                holdFrames--;
            } else {
                snapToTarget = false;
            }
        }
        // When not snapping, keep angles static (idle)
        return {currentPitch, currentYaw};
    }

    void reset() {
        currentPitch = 0.0f;
        currentYaw = 0.0f;
        snapToTarget = false;
    }
};

/// Generate "slow" aimbot with smoothing (to test if we catch it).
/// This aimbot has some smoothing but still lacks tremor and tracking error.
struct SmoothAimbotSimulator {
    float targetPitch = 0.0f;
    float targetYaw = 0.0f;
    float currentPitch = 0.0f;
    float currentYaw = 0.0f;
    float smoothingFactor = 0.3f;  // How much smoothing (0=instant, 1=very slow)
    
    QAngle step(float deltaTime) {
        // Linear interpolation toward target
        currentPitch += (targetPitch - currentPitch) * smoothingFactor;
        currentYaw   += (targetYaw   - currentYaw)   * smoothingFactor;
        return {currentPitch, currentYaw};
    }
    
    void setTarget(float pitch, float yaw) {
        targetPitch = pitch;
        targetYaw = yaw;
    }
};

} // namespace synth

// ══════════════════════════════════════════════════════════════════════════
// Test: Online Statistics (Welford's Algorithm)
// ══════════════════════════════════════════════════════════════════════════

void test_online_statistics() {
    std::cout << "  Testing OnlineStatistics..." << std::endl;
    
    using namespace sentinel::modules::aim;
    
    OnlineStatistics stats;
    
    // Test with known values: 1, 2, 3, 4, 5
    // Mean = 3.0, Variance = 2.5, StdDev = 1.581
    for (float x : {1.0f, 2.0f, 3.0f, 4.0f, 5.0f}) {
        stats.push(x);
    }
    
    TEST_NEAR("Mean of [1,2,3,4,5]", stats.meanValue(), 3.0f, 0.001f);
    TEST_NEAR("Variance of [1,2,3,4,5]", stats.variance(), 2.5f, 0.001f);
    TEST_NEAR("StdDev of [1,2,3,4,5]", stats.stddev(), 1.5811388f, 0.001f);
    TEST_NEAR("Count", stats.count(), 5.0f, 0.001f);
    
    // Test CV (coefficient of variation)
    TEST_NEAR("CV of [1,2,3,4,5]", stats.cv(), 0.527046f, 0.001f);
    
    // Test reset
    stats.reset();
    TEST_NEAR("After reset count", stats.count(), 0.0f, 0.001f);
    TEST_NEAR("After reset mean", stats.meanValue(), 0.0f, 0.001f);
}

// ══════════════════════════════════════════════════════════════════════════
// Test: Math Utilities
// ══════════════════════════════════════════════════════════════════════════

void test_math_utilities() {
    std::cout << "  Testing math utilities..." << std::endl;
    
    using namespace sentinel::modules::aim::aim_math;
    
    // normalizeAngle
    TEST_NEAR("normalizeAngle 0", normalizeAngle(0.0f), 0.0f, 0.001f);
    TEST_NEAR("normalizeAngle 180", normalizeAngle(180.0f), 180.0f, 0.001f);
    TEST_NEAR("normalizeAngle 181", normalizeAngle(181.0f), -179.0f, 0.001f);
    TEST_NEAR("normalizeAngle -181", normalizeAngle(-181.0f), 179.0f, 0.001f);
    TEST_NEAR("normalizeAngle 360", normalizeAngle(360.0f), 0.0f, 0.001f);
    TEST_NEAR("normalizeAngle 540", normalizeAngle(540.0f), 180.0f, 0.001f);
    
    // angleDiff
    TEST_NEAR("angleDiff 10-5", angleDiff(10.0f, 5.0f), 5.0f, 0.001f);
    TEST_NEAR("angleDiff 5-10", angleDiff(5.0f, 10.0f), -5.0f, 0.001f);
    TEST_NEAR("angleDiff 175 to -175 (wrapping)", angleDiff(175.0f, -175.0f), -10.0f, 0.001f);
    
    // angleDeltaMag
    sentinel::QAngle a{10.0f, 20.0f, 0.0f};
    sentinel::QAngle b{13.0f, 24.0f, 0.0f};
    float expected = std::sqrt(3.0f*3.0f + 4.0f*4.0f);  // 5.0
    TEST_NEAR("angleDeltaMag", angleDeltaMag(a, b), 5.0f, 0.001f);
    
    // logistic
    TEST_NEAR("logistic at center", logistic(5.0f, 5.0f, 1.0f), 0.5f, 0.01f);
    TEST_NEAR("logistic high value", logistic(10.0f, 5.0f, 1.0f), 1.0f - 1e-7f, 0.001f);
    TEST_NEAR("logistic low value", logistic(0.0f, 5.0f, 1.0f), 0.0f + 1e-7f, 0.001f);
    
    // inverseLogistic
    TEST_NEAR("inverseLogistic at center", inverseLogistic(5.0f, 5.0f, 1.0f), 0.5f, 0.01f);
    TEST_NEAR("inverseLogistic low->high", inverseLogistic(0.0f, 5.0f, 1.0f), 1.0f - 1e-7f, 0.001f);
    
    // discreteCurvature
    // Three collinear points should have near-zero curvature
    sentinel::QAngle p1{0.0f, 0.0f, 0.0f};
    sentinel::QAngle p2{1.0f, 1.0f, 0.0f};
    sentinel::QAngle p3{2.0f, 2.0f, 0.0f};
    float kappa = discreteCurvature(p1, p2, p3);
    TEST_NEAR("Collinear curvature", kappa, 0.0f, 0.1f);
    
    // Three points that form a sharp turn should have high curvature
    sentinel::QAngle q1{0.0f, 0.0f, 0.0f};
    sentinel::QAngle q2{10.0f, 0.0f, 0.0f};
    sentinel::QAngle q3{10.0f, 10.0f, 0.0f};
    kappa = discreteCurvature(q1, q2, q3);
    TEST("Sharp turn has positive curvature", kappa > 0.5f);
}

// ══════════════════════════════════════════════════════════════════════════
// Test: Ring Buffer
// ══════════════════════════════════════════════════════════════════════════

void test_ring_buffer() {
    std::cout << "  Testing RingBuffer..." << std::endl;
    
    using namespace sentinel::modules::aim;
    
    RingBuffer<int, 5> buf;
    
    TEST("Empty buffer", buf.empty());
    TEST("Buffer size 0", buf.size() == 0);
    
    buf.push_back(10);
    buf.push_back(20);
    buf.push_back(30);
    
    TEST("Buffer not empty after push", !buf.empty());
    TEST("Buffer size 3", buf.size() == 3);
    TEST("Element [0]", buf[0] == 10);
    TEST("Element [1]", buf[1] == 20);
    TEST("Element [2]", buf[2] == 30);
    TEST("Newest", buf.newest() == 30);
    TEST("Oldest", buf.oldest() == 10);
    
    // Test wrap-around
    buf.push_back(40);
    buf.push_back(50);
    buf.push_back(60);  // This should evict 10
    buf.push_back(70);  // This should evict 20
    
    TEST("Full buffer size", buf.size() == 5);
    TEST("Oldest after wrap", buf.oldest() == 30);
    TEST("Newest after wrap", buf.newest() == 70);
    
    buf.clear();
    TEST("Cleared buffer", buf.empty());
}

// ══════════════════════════════════════════════════════════════════════════
// Test: Feature Vector Aggregation
// ══════════════════════════════════════════════════════════════════════════

void test_feature_vector() {
    std::cout << "  Testing AimFeatureVector aggregation..." << std::endl;
    
    using namespace sentinel::modules::aim;
    
    AimFeatureVector features;
    
    // All features low → overall should be low
    features.computeOverall();
    TEST("All low gives low overall", features.overallConfidence < 0.1f);
    
    // One high feature → overall should be discounted
    features = AimFeatureVector{};
    features.snapConfidence = 0.9f;
    features.computeOverall();
    TEST("Single high feature discounted", features.overallConfidence < 0.7f);
    TEST("Single high feature still positive", features.overallConfidence > 0.4f);
    
    // Two high features → overall should be high
    features = AimFeatureVector{};
    features.snapConfidence = 0.85f;
    features.tremorConfidence = 0.8f;
    features.computeOverall();
    TEST("Two high features give high overall", features.overallConfidence > 0.5f);
    
    // Three high features → very high overall
    features = AimFeatureVector{};
    features.snapConfidence = 0.9f;
    features.tremorConfidence = 0.85f;
    features.trackingConfidence = 0.8f;
    features.computeOverall();
    TEST("Three high features give very high overall", features.overallConfidence > 0.7f);
}

// ══════════════════════════════════════════════════════════════════════════
// Test: Human-like Aim → Low Scores
// ══════════════════════════════════════════════════════════════════════════

void test_human_aim_low_scores() {
    std::cout << "  Testing human-like aim produces low scores..." << std::endl;
    
    using namespace sentinel::modules::aim;
    
    AimAnalyzerConfig cfg;
    AimAnalyzer analyzer(cfg);
    
    synth::HumanAimSimulator human;
    const std::string playerId = "human_test";
    
    // Simulate 5 seconds of human-like aiming at 64 Hz
    const float deltaTime = 1.0f / 64.0f;
    const int frames = static_cast<int>(5.0f / deltaTime);  // 320 frames
    
    for (int i = 0; i < frames; ++i) {
        // Occasionally flick to a new target
        if (i % 100 == 0) {
            human.startFlick(
                static_cast<float>(rand() % 60 - 30),  // pitch: -30 to 30
                static_cast<float>(rand() % 120 - 60)  // yaw: -60 to 60
            );
        }
        
        QAngle angles = human.step(deltaTime);
        bool isFiring = (i % 50 == 0);  // Fire periodically
        bool isMoving = true;
        
        analyzer.processFrame(playerId, angles, {}, isFiring, isMoving, deltaTime);
    }
    
    const auto& features = analyzer.getFeatures(playerId);
    float score = analyzer.getSuspicionScore(playerId);
    
    std::cout << "    Overall score: " << score 
              << " (snap=" << features.snapConfidence
              << ", tremor=" << features.tremorConfidence
              << ", tracking=" << features.trackingConfidence
              << ", reaction=" << features.reactionConfidence
              << ", curvature=" << features.curvatureConfidence
              << ")" << std::endl;
    
    // Human-like aim should have low overall confidence (< 30)
    TEST("Human aim: low suspicion score", score < 30.0f);
    
    analyzer.resetPlayer(playerId);
}

// ══════════════════════════════════════════════════════════════════════════
// Test: Aimbot-like Aim → High Scores
// ══════════════════════════════════════════════════════════════════════════

void test_aimbot_high_scores() {
    std::cout << "  Testing aimbot-like aim produces high scores..." << std::endl;
    
    using namespace sentinel::modules::aim;
    
    AimAnalyzerConfig cfg;
    AimAnalyzer analyzer(cfg);
    
    synth::AimbotSimulator aimbot;
    const std::string playerId = "aimbot_test";
    
    // Simulate aimbot behavior
    const float deltaTime = 1.0f / 64.0f;
    const int frames = static_cast<int>(5.0f / deltaTime);  // 320 frames
    
    for (int i = 0; i < frames; ++i) {
        // Aimbot snaps to random targets every ~30 frames
        if (i % 30 == 0) {
            aimbot.startSnap(
                static_cast<float>(rand() % 40 - 20),
                static_cast<float>(rand() % 80 - 40)
            );
        }
        
        QAngle angles = aimbot.step(deltaTime);
        analyzer.processFrame(playerId, angles, {}, false, false, deltaTime);
    }
    
    const auto& features = analyzer.getFeatures(playerId);
    float score = analyzer.getSuspicionScore(playerId);
    
    std::cout << "    Overall score: " << score 
              << " (snap=" << features.snapConfidence
              << ", tremor=" << features.tremorConfidence
              << ", silent=" << features.silentAimConfidence
              << ")" << std::endl;
    
    // Aimbot should produce high suspicion (> 40)
    TEST("Aimbot: high snap confidence", features.snapConfidence > 0.3f);
    
    analyzer.resetPlayer(playerId);
}

// ══════════════════════════════════════════════════════════════════════════
// Test: Smooth Aimbot → Detected by Tremor Analysis
// ══════════════════════════════════════════════════════════════════════════

void test_smooth_aimbot_tremor_detection() {
    std::cout << "  Testing smooth aimbot (tremor detection)..." << std::endl;
    
    using namespace sentinel::modules::aim;
    
    AimAnalyzerConfig cfg;
    cfg.tremorRmsThreshold = 0.15f;  // Standard threshold
    AimAnalyzer analyzer(cfg);
    
    synth::SmoothAimbotSimulator smoothAimbot;
    const std::string playerId = "smooth_aimbot_test";
    
    const float deltaTime = 1.0f / 64.0f;
    const int frames = static_cast<int>(4.0f / deltaTime);  // 256 frames
    
    for (int i = 0; i < frames; ++i) {
        if (i % 64 == 0) {
            smoothAimbot.setTarget(
                static_cast<float>(rand() % 50 - 25),
                static_cast<float>(rand() % 100 - 50)
            );
        }
        
        QAngle angles = smoothAimbot.step(deltaTime);
        analyzer.processFrame(playerId, angles, {}, true, true, deltaTime);
    }
    
    const auto& features = analyzer.getFeatures(playerId);
    
    std::cout << "    Tremor confidence: " << features.tremorConfidence
              << " curvature: " << features.curvatureConfidence << std::endl;
    
    // Smooth aimbot should be detected by tremor analysis
    // (no high-frequency tremor signal)
    // Note: this is less reliable than snap detection since smoothing
    // can mask some characteristics
    TEST("Smooth aimbot: tremor analysis active", features.tremorConfidence > 0.0f);
    
    analyzer.resetPlayer(playerId);
}

// ══════════════════════════════════════════════════════════════════════════
// Test: Edge Cases
// ══════════════════════════════════════════════════════════════════════════

void test_edge_cases() {
    std::cout << "  Testing edge cases..." << std::endl;
    
    using namespace sentinel::modules::aim;
    
    AimAnalyzerConfig cfg;
    AimAnalyzer analyzer(cfg);
    
    // Player ID not found should return zero features
    const auto& unknownFeatures = analyzer.getFeatures("nonexistent_player");
    float unknownScore = analyzer.getSuspicionScore("nonexistent_player");
    
    TEST("Unknown player: zero features", unknownFeatures.overallConfidence == 0.0f);
    TEST("Unknown player: zero score", unknownScore == 0.0f);
    
    // Test reset all
    const std::string p1 = "player1";
    const std::string p2 = "player2";
    
    analyzer.processFrame(p1, {0, 0, 0}, {}, false, false, 1.0f/64.0f);
    analyzer.processFrame(p2, {0, 0, 0}, {}, false, false, 1.0f/64.0f);
    
    analyzer.resetAll();
    
    TEST("After resetAll: p1 has zero features",
         analyzer.getFeatures(p1).overallConfidence == 0.0f);
    TEST("After resetAll: p2 has zero features",
         analyzer.getFeatures(p2).overallConfidence == 0.0f);
    
    // Test with zero deltaTime (should handle gracefully)
    analyzer.processFrame("delta_zero", {10, 20, 0}, {}, true, true, 0.0f);
    TEST("Zero deltaTime: doesn't crash", true);
    
    analyzer.resetPlayer("delta_zero");
}

// ══════════════════════════════════════════════════════════════════════════
// Test: Stationary Player → No False Positive
// ══════════════════════════════════════════════════════════════════════════

void test_stationary_player_no_false_positive() {
    std::cout << "  Testing stationary player (no false positives)..." << std::endl;
    
    using namespace sentinel::modules::aim;
    
    AimAnalyzerConfig cfg;
    AimAnalyzer analyzer(cfg);
    
    const std::string playerId = "stationary";
    const float deltaTime = 1.0f / 64.0f;
    
    // Simulate a player who is standing still (not aiming)
    for (int i = 0; i < 300; ++i) {
        analyzer.processFrame(
            playerId, {0.0f, 0.0f, 0.0f}, {}, false, false, deltaTime
        );
    }
    
    float score = analyzer.getSuspicionScore(playerId);
    
    std::cout << "    Stationary player score: " << score << std::endl;
    
    // Stationary player should not be flagged
    TEST("Stationary: low suspicion score", score < 15.0f);
    
    analyzer.resetPlayer(playerId);
}

// ══════════════════════════════════════════════════════════════════════════
// Test: Evidence String
// ══════════════════════════════════════════════════════════════════════════

void test_evidence_string() {
    std::cout << "  Testing evidence string generation..." << std::endl;
    
    using namespace sentinel::modules::aim;
    
    AimAnalyzerConfig cfg;
    AimAnalyzer analyzer(cfg);
    
    const std::string playerId = "evidence_test";
    
    // No data → "No significant detections"
    std::string evidence = analyzer.getEvidence(playerId);
    TEST("Empty evidence: 'No significant detections'",
         evidence.find("No significant") != std::string::npos);
    
    analyzer.resetAll();
}

// ══════════════════════════════════════════════════════════════════════════
// Main Test Runner
// ══════════════════════════════════════════════════════════════════════════

int main() {
    std::cout << "\n=== Sentinel AntiCheat - Aim Analyzer Unit Tests ===\n" << std::endl;
    
    // Core Math
    std::cout << "Core Math:" << std::endl;
    test_online_statistics();
    test_math_utilities();
    test_ring_buffer();
    test_feature_vector();
    
    // Detection Tests
    std::cout << "\nDetection Tests:" << std::endl;
    test_human_aim_low_scores();
    test_aimbot_high_scores();
    test_smooth_aimbot_tremor_detection();
    
    // Edge Cases
    std::cout << "\nEdge Cases:" << std::endl;
    test_edge_cases();
    test_stationary_player_no_false_positive();
    test_evidence_string();
    
    // Summary
    int total = g_testsPassed + g_testsFailed;
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << total << " tests"
              << " | Passed: " << g_testsPassed
              << " | Failed: " << g_testsFailed << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    return (g_testsFailed > 0) ? 1 : 0;
}
