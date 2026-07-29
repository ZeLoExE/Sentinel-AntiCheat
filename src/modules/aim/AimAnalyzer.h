// ============================================================================
// Sentinel AntiCheat - Human Aim Analyzer
// ============================================================================
// Statistical analysis of player aim behavior using angular kinematics.
// 
// Detection Methodology:
//   Human aim follows known statistical distributions derived from motor
//   control physiology. Cheat aim deviates from these in measurable ways:
//   - Excessive smoothness (no physiological tremor)
//   - Impossible angular acceleration (instantaneous velocity changes)
//   - Absent correction micro-adjustments (perfect first-attempt tracking)
//   - Sub-physiological reaction times (< 100ms sustained)
//   - Unnatural trajectory curvature (straight-line mouse paths)
//
// All detection produces confidence scores via logistic mapping from
// statistical distance to human baselines. No binary classifications.
// ============================================================================

#pragma once
#include "AimAnalysisTypes.h"
#include "core/IDetector.h"
#include "core/IPlayerManager.h"
#include "core/ILogger.h"
#include <unordered_map>
#include <mutex>
#include <array>

namespace sentinel::modules::aim {

/// Threshold configuration for the aim analyzer.
/// Each value is a statistical parameter with documented justification.
struct AimAnalyzerConfig {
    // ── Snap/Flick Analysis ────────────────────────────────────────────
    float snapVelocityThreshold  = 1500.0f;  // deg/s: 1 frame at 64 Hz
                                             // 1500 deg/s ≈ 23 deg/frame
                                             // Justification: human flicks max
                                             // ~1000-2000 deg/s depending on DPI.
                                             // Above 1500 is suspiciously fast.
    
    uint32_t minSnapAccelFrames  = 2;       // Minimum human acceleration phase.
                                            // Human muscle activation takes
                                            // ~30ms minimum = 2 frames at 64 Hz.
                                            // 0-1 frames = electronic.

    // ── Tremor Analysis ────────────────────────────────────────────────
    float tremorAnalysisAlpha     = 0.15f;  // EMA alpha for tremor HPF.
                                            // Corner freq ≈ alpha * fps / 2π
                                            // At 64 Hz: 0.15 * 64 / 6.28 ≈ 1.5 Hz
                                            // Passes tremor band (6-14 Hz).
    
    float tremorRmsThreshold      = 0.15f;  // deg/s²: Minimum RMS tremor.
                                            // Human physiological tremor RMS
                                            // is 0.3-0.8 deg/s² during steady
                                            // aim. Below 0.15 is unnatural.

    // ── Reaction Time ──────────────────────────────────────────────────
    float impossibleReactionMs    = 100.0f; // 100ms = physiological floor.
                                            // Olympic sprinters react at ~100ms.
                                            // Sustained sub-100ms is impossible.
    
    float suspiciousReactionMs    = 150.0f; // 150ms = elite player territory.
                                            // Pro CS players average ~180-220ms.
    
    uint32_t minReactionEvents    = 5;      // Minimum events before analysis.

    // ── Tracking Stability ─────────────────────────────────────────────
    float perfectTrackThreshold   = 0.5f;   // degrees RMS: Cheat-level tracking.
                                            // Human tracking RMS is 2-5 degrees.
                                            // Below 0.5 is suspicious.
    
    float naturalTrackThreshold   = 2.0f;   // degrees RMS: Elite human tracking.
                                            // Anything below this could be
                                            // legitimate aiming skill.

    // ── Curvature Analysis ─────────────────────────────────────────────
    float lowCurvatureThreshold   = 0.1f;   // Mean curvature below this is
                                            // unnaturally straight.
                                            // Human curvature μ ≈ 0.8.
    
    float curvatureStdThreshold   = 0.3f;   // Curvature stddev below this
                                            // is unnaturally consistent.

    // ── Confidences ────────────────────────────────────────────────────
    float snapBaseConfidence      = 0.7f;   // Max confidence from snap alone
    float silentAimBaseConfidence = 0.8f;   // Max confidence from silent aim
    float trackingBaseConfidence  = 0.6f;   // Max confidence from tracking
    float tremorBaseConfidence    = 0.5f;   // Max confidence from tremor
    float reactionBaseConfidence  = 0.6f;   // Max confidence from reactions
    float curvatureBaseConfidence = 0.4f;   // Max confidence from curvature
};

// ══════════════════════════════════════════════════════════════════════════
// AimAnalyzer
// ══════════════════════════════════════════════════════════════════════════

/// Statistical analyzer for human aim characteristics.
///
/// This class implements the core detection pipeline:
///   Raw Angles → Kinematics → Feature Extraction → Confidence Aggregation
///
/// Every method is mathematically justified and references the relevant
/// human motor control literature where applicable.
class AimAnalyzer {
public:
    explicit AimAnalyzer(const AimAnalyzerConfig& cfg);
    ~AimAnalyzer() = default;

    // ── Main Pipeline ──────────────────────────────────────────────────

    /// Process one frame of angular data.
    /// This is the primary entry point for the per-frame update loop.
    /// @param playerId Unique player identifier
    /// @param angles Current view angles (from PlayerContext)
    /// @param punch Current punch/recoil angles
    /// @param isFiring Whether the player is attacking this frame
    /// @param isMoving Whether the player is moving significantly
    /// @param deltaTime Seconds since last frame
    void processFrame(
        const std::string& playerId,
        const QAngle& angles,
        const QAngle& punch,
        bool isFiring,
        bool isMoving,
        float deltaTime
    );

    /// Report a visibility change (enemy became visible to player).
    /// Called when a server-side LOS trace confirms enemy visibility.
    /// @param playerId The player who can see the enemy
    /// @param enemyPos World position of the visible enemy
    /// @param enemyDistance Distance to enemy
    void reportEnemyVisible(
        const std::string& playerId,
        const Vector3& enemyPos,
        float enemyDistance
    );

    /// Report a kill event for analysis.
    void reportKill(
        const std::string& playerId,
        const Vector3& enemyPos,
        bool headshot
    );

    /// Get the current feature vector for a player.
    /// @note Features are computed lazily on access.
    [[nodiscard]] const AimFeatureVector& getFeatures(
        const std::string& playerId
    );

    /// Get suspicion score from features (0-100).
    [[nodiscard]] float getSuspicionScore(
        const std::string& playerId
    );

    /// Get evidence string for the highest-confidence detection.
    [[nodiscard]] std::string getEvidence(
        const std::string& playerId
    );

    /// Reset all data for a player.
    void resetPlayer(const std::string& playerId);

    /// Reset all players.
    void resetAll();

    // ── Per-Frame Internal Pipeline (called by processFrame) ─────────────

private:
    /// Step 1: Compute angular kinematics from raw angles.
    void computeKinematics(
        AimAnalysisState& state,
        const QAngle& angles,
        float deltaTime
    );

    /// Step 2: Detect and analyze snap/flick events.
    void detectSnaps(
        AimAnalysisState& state,
        const QAngle& angles,
        float angularVelocity
    );

    /// Step 3: Update tremor analysis (high-pass velocity filter).
    void updateTremorAnalysis(
        AimAnalysisState& state,
        float angularVelocity,
        float deltaTime
    );

    /// Step 4: Update tracking stability metrics.
    void updateTracking(
        AimAnalysisState& state,
        const std::string& playerId,
        const QAngle& angles,
        bool isFiring
    );

    /// Step 5: Update trajectory curvature statistics.
    void updateCurvature(
        AimAnalysisState& state,
        const QAngle& angles
    );

    /// Step 6: Compute all feature confidences from accumulated statistics.
    AimFeatureVector computeFeatures(
        AimAnalysisState& state
    );

    // ── Feature Computation Methods ────────────────────────────────────

    /// Snap confidence: based on acceleration frames and snap count.
    /// Uses binomial test: probability of k snaps with ≤1 accel frame
    /// out of N total if human (p = 0.02 for ≤1 frame snap).
    float computeSnapConfidence(const AimAnalysisState& state) const;

    /// Silent aim confidence: based on zero-velocity frames between snaps.
    /// Silent aim shows NO intermediate frames between positions.
    float computeSilentAimConfidence(const AimAnalysisState& state) const;

    /// Tracking confidence: based on RMS error compared to human baseline.
    float computeTrackingConfidence(const AimAnalysisState& state) const;

    /// Tremor confidence: based on high-frequency RMS compared to human baseline.
    float computeTremorConfidence(const AimAnalysisState& state) const;

    /// Reaction confidence: based on reaction time distribution.
    float computeReactionConfidence(const AimAnalysisState& state) const;

    /// Curvature confidence: based on straight-line path fraction.
    float computeCurvatureConfidence(const AimAnalysisState& state) const;

    /// Correction confidence: based on presence of micro-adjustments.
    float computeCorrectionConfidence(const AimAnalysisState& state) const;

    // ── Helpers ────────────────────────────────────────────────────────

    AimAnalysisState& getState(const std::string& playerId);

    const AimAnalyzerConfig m_cfg;
    std::unordered_map<std::string, AimAnalysisState> m_playerStates;
    mutable std::mutex m_mutex;
};

} // namespace sentinel::modules::aim
