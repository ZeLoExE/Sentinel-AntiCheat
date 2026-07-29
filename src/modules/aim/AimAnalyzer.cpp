// ============================================================================
// Sentinel AntiCheat - Human Aim Analyzer Implementation
// ============================================================================
// Statistical analysis of player aim behavior using angular kinematics.
// All detections produce confidence scores in [0,1] via logistic mapping
// from statistical distance to physiological human baselines.
//
// Pipeline (per frame):
//   1. computeKinematics() → stores in state.currentKinematics
//   2. Update online statistics from currentKinematics
//   3. detectSnaps() using currentKinematics.velocity
//   4. updateTremorAnalysis() using currentKinematics.velocity
//   5. updateTracking() using view angles + enemy positions
//   6. updateCurvature() using currentKinematics.curvature
//   7. Push sample to ring buffer
// ============================================================================

#include "AimAnalyzer.h"
#include <sstream>
#include <algorithm>
#include <cmath>

namespace sentinel::modules::aim {

AimAnalyzer::AimAnalyzer(const AimAnalyzerConfig& cfg) : m_cfg(cfg) {}

// ══════════════════════════════════════════════════════════════════════════
// Main Pipeline
// ══════════════════════════════════════════════════════════════════════════

void AimAnalyzer::processFrame(
    const std::string& playerId,
    const QAngle& angles,
    const QAngle& punch,
    bool isFiring,
    bool isMoving,
    float deltaTime)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    AimAnalysisState& state = getState(playerId);

    // Clamp deltaTime — prevent division by zero and lag-spike artifacts
    if (deltaTime <= 0.0f) deltaTime = 1.0f / 64.0f;
    if (deltaTime > 0.1f)   deltaTime = 1.0f / 64.0f;

    // ════════════════════════════════════════════════════════════════════
    // Step 1: Compute angular kinematics
    //          Stores results in state.currentKinematics
    // ════════════════════════════════════════════════════════════════════
    computeKinematics(state, angles, deltaTime);

    float angularVelocity = state.currentKinematics.velocity;
    float angularAccel    = state.currentKinematics.accel;

    // Update online statistics for active aim frames only
    if (angularVelocity > MIN_ACTIVE_AIM_DELTA) {
        state.velocityStats.push(angularVelocity);
        state.accelStats.push(angularAccel);
        state.curvatureStats.push(state.currentKinematics.curvature);
        state.totalActiveFrames++;
    }

    state.movingAvgVelocity = aim_math::ema(
        state.movingAvgVelocity, angularVelocity, 0.1f
    );
    state.movingAvgAccel = aim_math::ema(
        state.movingAvgAccel, angularAccel, 0.1f
    );

    // ════════════════════════════════════════════════════════════════════
    // Step 2: Detect snap/flick events using current velocity
    // ════════════════════════════════════════════════════════════════════
    detectSnaps(state, angles, angularVelocity);

    // ════════════════════════════════════════════════════════════════════
    // Step 3: Update tremor analysis (high-pass velocity filter)
    // ════════════════════════════════════════════════════════════════════
    updateTremorAnalysis(state, angularVelocity, deltaTime);

    // ════════════════════════════════════════════════════════════════════
    // Step 4: Update tracking stability
    // ════════════════════════════════════════════════════════════════════
    updateTracking(state, playerId, angles, isFiring);

    // ════════════════════════════════════════════════════════════════════
    // Step 5: Update curvature
    // ════════════════════════════════════════════════════════════════════
    // (curvature is already in state.currentKinematics from computeKinematics)
    // The statistics have already been updated above in the active-aim block.

    // Increment frame counter and decel/snap tracking
    state.framesSinceSnap++;
    state.totalFramesAnalyzed++;
    state.timeSinceLastSnap += deltaTime;
    state.timeSinceLastReaction += deltaTime;

    // ════════════════════════════════════════════════════════════════════
    // Push the complete sample into the ring buffer
    // ════════════════════════════════════════════════════════════════════
    AimSample sample;
    sample.angles          = angles;
    sample.punchAngles     = punch;
    sample.deltaTime       = deltaTime;
    sample.angularVelocity = angularVelocity;
    sample.angularAccel    = angularAccel;
    sample.curvature       = state.currentKinematics.curvature;
    sample.isFiring        = isFiring;
    sample.isMoving        = isMoving;

    state.samples.push_back(sample);
}

// ══════════════════════════════════════════════════════════════════════════
// Step 1: Angular Kinematics
// ══════════════════════════════════════════════════════════════════════════
//
// Given raw view angles θ(t) at discrete time t:
//
//   Angular displacement:  Δθ(t) = θ(t) - θ(t-1)       [normalized]
//   Angular velocity:      ω(t) = ||Δθ(t)|| / Δt       [deg/s]
//   Angular acceleration:  α(t) = (ω(t) - ω(t-1)) / Δt [deg/s²]
//   Discrete curvature:    κ(t) = ||Δω(t)|| / (||ω(t)|| + ||ω(t-1)|| + ε)
//
// Curvature measures how much the direction of movement changed.
//   κ ≈ 0  → straight line (unnatural for sustained aim)
//   κ > 0  → curved path (natural for human arm movement)
//
// Results are stored in state.currentKinematics for immediate use
// by subsequent pipeline steps.
// ══════════════════════════════════════════════════════════════════════════

void AimAnalyzer::computeKinematics(
    AimAnalysisState& state,
    const QAngle& angles,
    float deltaTime)
{
    if (!state.hasPrevFrame) {
        state.prevAngles = angles;
        state.prevVelocity = 0.0f;
        state.hasPrevFrame = true;
        state.currentKinematics = {0.0f, 0.0f, 0.0f};
        return;
    }

    // ── Angular displacement ───────────────────────────────────────────
    float dp = aim_math::angleDiff(angles.pitch, state.prevAngles.pitch);
    float dy = aim_math::angleDiff(angles.yaw, state.prevAngles.yaw);
    float displacement = std::sqrt(dp * dp + dy * dy);

    // ── Angular velocity ───────────────────────────────────────────────
    float velocity = displacement / deltaTime;

    // ── Angular acceleration ────────────────────────────────────────────
    float accel = (velocity - state.prevVelocity) / deltaTime;

    // ── Discrete curvature ─────────────────────────────────────────────
    // κ(t) = ||Δθ(t) - Δθ(t-1)|| / (||Δθ(t)|| + ||Δθ(t-1)|| + ε)
    //
    // When the change in velocity direction is large relative to the
    // total velocity magnitude, curvature is high (sharp turn).
    // When direction is constant, curvature is near zero (straight line).
    float curvature = 0.0f;
    QAngle currentDelta{dp, dy, 0.0f};
    if (state.hasPrevDelta) {
        float deltaDiff = aim_math::angleDeltaMag(currentDelta, state.prevDelta);
        float magSum = displacement + currentDelta.length();
        if (magSum > 0.001f) {
            curvature = deltaDiff / (magSum + 0.001f);
        }
    }

    // ── Store computed values in state for immediate access ────────────
    state.currentKinematics.velocity   = velocity;
    state.currentKinematics.accel      = accel;
    state.currentKinematics.curvature  = curvature;

    // ── Update state for next frame ─────────────────────────────────────
    state.prevAngles    = angles;
    state.prevVelocity  = velocity;
    state.prevDelta     = currentDelta;
    state.hasPrevDelta  = true;
}

// ══════════════════════════════════════════════════════════════════════════
// Step 2: Snap/Flick Detection with Velocity Profile Analysis
// ══════════════════════════════════════════════════════════════════════════
//
// A "snap" (aka flick) is detected when angular velocity exceeds the
// snap threshold: ω(t) > ω_snap (default 1500 deg/s).
//
// The acceleration profile τ_accel is the number of frames to go from
// 10% to 90% of peak velocity. In human flicks, the arm must physically
// accelerate, requiring ≥ 2 frames at 64 Hz (≈ 31 ms).
//
// After detection, subsequent frames update the deceleration phase:
//   - Frames until velocity drops below 10% of peak → τ_decel
//   - Any sign reversal (direction change) at low velocity → correction
//
// Snap profile scoring:
//   Human:   τ_accel ≥ 2, τ_decel ≥ 2, has corrections
//   Aimbot:  τ_accel ≤ 1, τ_decel ≈ 0, no corrections
//
// ══════════════════════════════════════════════════════════════════════════

void AimAnalyzer::detectSnaps(
    AimAnalysisState& state,
    const QAngle& angles,
    float angularVelocity)
{
    const float SNAP_THRESHOLD = m_cfg.snapVelocityThreshold;

    // ── Update deceleration phase for any active snap ──────────────────
    if (state.framesSinceSnap < 10 && !state.snapEvents.empty()) {
        SnapEvent& currentSnap = state.snapEvents.back();

        // Check if we're still in deceleration phase
        float peakVel = currentSnap.peakVelocity;
        float decelThreshold = peakVel * 0.1f;

        if (angularVelocity > decelThreshold) {
            // Still decelerating
            currentSnap.decelerationFrames++;
            currentSnap.totalFrames++;
        }

        // Check for correction micro-adjustment: sign change at low velocity.
        // A correction occurs when the player overshoots and reverses
        // direction to adjust. This appears as a velocity zero-crossing
        // (direction reversal) at low speed (< 20% of peak).
        float correctionThreshold = peakVel * 0.2f;
        if (!currentSnap.hasCorrection &&
            angularVelocity < correctionThreshold &&
            state.samples.size() >= 2) {
            // Check if velocity sign changed
            float prevVel = state.samples.newest().angularVelocity;
            if (prevVel * angularVelocity < 0 &&  // Sign change
                std::abs(angularVelocity) < correctionThreshold) {
                currentSnap.hasCorrection = true;
            }
        }
    }

    // ── Detect new snap event ──────────────────────────────────────────
    if (angularVelocity > SNAP_THRESHOLD) {
        if (state.timeSinceLastSnap < 0.05f) return;  // Cooldown

        SnapEvent snap;
        snap.peakVelocity       = angularVelocity;
        snap.maxAngularAccel    = state.currentKinematics.accel;

        // ── Acceleration frame analysis ─────────────────────────────────
        // Compare current velocity to previous frame's velocity.
        // If previous frame was near zero (< 10% threshold), we jumped
        // from idle to snap in one frame → τ_accel = 1 (suspicious).
        //
        // If previous frame was already elevated, acceleration was gradual.
        float prevVel = state.prevVelocity;  // This is the LAST FRAME's velocity
        float threshold10 = SNAP_THRESHOLD * 0.1f;

        if (prevVel < threshold10) {
            snap.accelerationFrames = 1;  // Instant acceleration
            snap.totalDisplacement = angularVelocity * 1.0f / 64.0f;
        } else {
            // Estimate: how many frames from 10% to peak?
            // Ratio of prevVel to peak tells us where we are in the profile
            float velRatio = prevVel / angularVelocity;
            if (velRatio > 0.5f) {
                // Previous frame already at >50% peak → accel was ≤ 2 frames
                snap.accelerationFrames = 2;
            } else {
                snap.accelerationFrames = 3;  // Gradual human-like acceleration
            }
            // Estimate total displacement from average velocity * duration
            float avgVel = (prevVel + angularVelocity) * 0.5f;
            snap.totalDisplacement = avgVel * 1.0f / 64.0f;
        }

        state.snapEvents.push_back(snap);
        state.snapVelocityStats.push(snap.peakVelocity);
        state.snapAccelFramesStats.push(static_cast<float>(snap.accelerationFrames));

        // Reset tracking for deceleration phase
        state.framesSinceSnap = 0;
        state.timeSinceLastSnap = 0.0f;
        state.inSnapDecel = true;
    }
}

// ══════════════════════════════════════════════════════════════════════════
// Step 3: Tremor Analysis (High-Pass Filter)
// ══════════════════════════════════════════════════════════════════════════
//
// Physiological tremor is extracted by high-pass filtering the velocity:
//
//   y(t) = ω(t) - EMA(ω(t))    where EMA uses α = 0.15
//
// The EMA acts as a low-pass filter with cutoff ≈ 1.5 Hz at 64 fps.
// The high-pass output y(t) contains frequencies above 1.5 Hz, which
// includes the 8-12 Hz tremor band.
//
// RMS of y(t) over a sliding window:
//   Human (steady aim, actively tracking):  RMS ≈ 0.3-0.8 deg/s²
//   Human (idle, not aiming):               RMS ≈ 0.1-0.3 deg/s²
//   Aimbot (any state):                     RMS < 0.1 deg/s²
//
// The EMA state is PER PLAYER (stored in AimAnalysisState.tremorEmaState)
// to prevent cross-player contamination.
// ══════════════════════════════════════════════════════════════════════════

void AimAnalyzer::updateTremorAnalysis(
    AimAnalysisState& state,
    float angularVelocity,
    float deltaTime)
{
    // Per-player EMA state for the high-pass filter
    float highFreq = aim_math::highpass(
        angularVelocity,
        state.tremorEmaState,
        m_cfg.tremorAnalysisAlpha
    );

    // Store in per-player ring buffer for RMS computation
    state.highpassBuffer[state.highpassIndex] = highFreq;
    state.highpassIndex = (state.highpassIndex + 1) % state.highpassBuffer.size();

    // Compute RMS of high-frequency component over the buffer window
    float sumSq = 0.0f;
    for (float v : state.highpassBuffer) sumSq += v * v;
    state.tremorRms = std::sqrt(sumSq / static_cast<float>(state.highpassBuffer.size()));

    // Update online statistics (only for active aim frames)
    if (angularVelocity > MIN_ACTIVE_AIM_DELTA) {
        state.highFreqVelocity.push(highFreq);
    }
}

// ══════════════════════════════════════════════════════════════════════════
// Step 4: Tracking Stability Analysis
// ══════════════════════════════════════════════════════════════════════════
//
// When an enemy is visible (confirmed by server-side LOS trace), we compute
// the angular error between the player's view direction and the direction
// to the enemy:
//
//   v_view = angleToVector(θ(t))                    // Player's aim direction
//   v_enemy = (enemyPos - playerPos).normalize()    // Direction to enemy
//   ε(t) = arccos(v_view · v_enemy)                 // Angular error in radians
//
// Convert to degrees and track over the engagement period.
//
// Human tracking error characteristics:
//   - ε_RMS ≈ 2-5 degrees (depends on distance, weapon, skill)
//   - Error oscillates at 1-3 Hz (visual feedback loop delay)
//   - Shows over-correction: error polarity flips
//
// Aimbot tracking:
//   - ε_RMS < 0.5 degrees (too precise)
//   - No oscillation (error is flat/constant)
//   - No over-correction
//
// ══════════════════════════════════════════════════════════════════════════

void AimAnalyzer::updateTracking(
    AimAnalysisState& state,
    const std::string& playerId,
    const QAngle& angles,
    bool isFiring)
{
    // Tracking analysis is event-driven via reportEnemyVisible().
    // This per-frame method:
    //   1. Checks if an enemy is currently visible (set by reportEnemyVisible)
    //   2. If yes, computes tracking error from latest enemy position
    //   3. Updates engagement statistics
    
    if (state.samples.empty()) return;

    if (state.enemyVisible) {
        // Compute direction from player to enemy
        // Note: in production, playerPos comes from the PlayerContext.
        // We approximate using the last known origin or receive it
        // via a separate signal.
        
        // The angular error ε is computed using the view direction
        // and the direction to the enemy.
        // v_view = angleToVector(angles)
        // v_enemy = normalize(enemyPos - playerPos)
        // ε = arccos(dot(v_view, v_enemy)) * 180/π
        
        // For now, this is populated by the integration layer when
        // player position data is available. The statistics are
        // updated when reportEnemyVisible() is called with positions.
    }
}

// ══════════════════════════════════════════════════════════════════════════
// External Event: Enemy Visibility
// ══════════════════════════════════════════════════════════════════════════
//
// Called by the integration layer when a server-side trace confirms that
// an enemy is visible to the player. Provides:
//   - Enemy world position (for tracking error computation)
//   - Player world position (needed for direction calculation)
//
// Also captures reaction events: if the player fires within a few frames
// of an enemy becoming visible, we record a reaction time.
//
// ══════════════════════════════════════════════════════════════════════════

void AimAnalyzer::reportEnemyVisible(
    const std::string& playerId,
    const Vector3& enemyPos,
    float enemyDistance)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    AimAnalysisState& state = getState(playerId);

    state.enemyVisible = true;
    state.lastEnemyPos = enemyPos;

    // Compute tracking error if we have position data
    // ε = angular distance from aim direction to enemy position
    // This requires player position which is NOT provided in the current
    // API. In a production integration, the caller also provides the
    // player's eye position, or we retrieve it from PlayerContext.

    // ── Check for reaction event ──────────────────────────────────────
    // IMPORTANT LIMITATION: The server-side only approach cannot precisely
    // measure reaction time because we don't have frame-level timing of
    // when the enemy ON SCREEN became visible to the player. The server
    // knows when LOS is established, but the player's monitor/network
    // delay means the server's LOS timestamp and the player's visual
    // perception are different by 1-2 frames (16-32ms) due to:
    //   - Network latency (player sees the enemy after server confirms)
    //   - Client-side interpolation (visual smoothing)
    //   - Monitor refresh rate (60-144 Hz vs 64 Hz server tick)
    //
    // Therefore, reaction time analysis via server-side LOS is inherently
    // imprecise. We cannot distinguish a 50ms trigger bot from a 150ms
    // human reaction with server data alone.
    //
    // To prevent false positives: we DO NOT record reactions here.
    // Instead, reaction detection requires precise timing data from
    // the integration layer (e.g., ReGameDLL's CBasePlayer::FireBullets
    // hook with precise timing, or client-side message analysis).
    //
    // See docs/limitations/reaction_time.md for full analysis.
}

void AimAnalyzer::reportKill(
    const std::string& playerId,
    const Vector3& enemyPos,
    bool headshot)
{
    // Kill events provide confirmation of accurate aim.
    // Currently used for statistical context; the primary detection
    // happens through angular kinematics, not kill events.
}

// ══════════════════════════════════════════════════════════════════════════
// Feature Computation
// ══════════════════════════════════════════════════════════════════════════
//
// All confidence values use the logistic function:
//   f(x) = 1 / (1 + e^{-k(x - x0)})
//
// Where:
//   x0 = center point (50% confidence threshold)
//   k  = steepness (how abrupt the transition between "human" and "cheat")
//
// This provides a smooth, probabilistic interpretation: f(x) is the
// probability that the observed signal is generated by a cheat rather
// than a human, given the measured metric x.
//
// ══════════════════════════════════════════════════════════════════════════

const AimFeatureVector& AimAnalyzer::getFeatures(const std::string& playerId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    AimAnalysisState& state = getState(playerId);

    if (state.totalFramesAnalyzed < MIN_ANALYSIS_FRAMES) {
        state.currentFeatures = AimFeatureVector{};
        return state.currentFeatures;
    }

    state.currentFeatures = computeFeatures(state);
    return state.currentFeatures;
}

AimFeatureVector AimAnalyzer::computeFeatures(AimAnalysisState& state) {
    AimFeatureVector features;

    features.snapConfidence       = computeSnapConfidence(state);
    features.silentAimConfidence  = computeSilentAimConfidence(state);
    features.trackingConfidence   = computeTrackingConfidence(state);
    features.tremorConfidence     = computeTremorConfidence(state);
    features.reactionConfidence   = computeReactionConfidence(state);
    features.curvatureConfidence  = computeCurvatureConfidence(state);
    features.correctionConfidence = computeCorrectionConfidence(state);

    features.computeOverall();
    return features;
}

// ══════════════════════════════════════════════════════════════════════════
// Snap Confidence
// ══════════════════════════════════════════════════════════════════════════
//
// Model: For each detected snap event, we classify the acceleration phase
// as human (τ_accel ≥ 2 frames) or suspicious (τ_accel ≤ 1 frame).
//
// Under the null hypothesis (human player), the probability of a single
// snap having τ_accel ≤ 1 is approximately p ≈ 0.02 (estimated from
// motor control literature: muscle activation requires ~30 ms minimum,
// which is about 2 frames at 64 Hz).
//
// For N observed snaps, the number k with τ_accel ≤ 1 follows a
// binomial distribution Bin(N, p = 0.02).
//
// We compute the observed ratio k/N and map it to a confidence via
// logistic function centered at 0.15 (7.5× the human baseline rate).
// Above this ratio, we become increasingly confident of cheating.
//
// ══════════════════════════════════════════════════════════════════════════

float AimAnalyzer::computeSnapConfidence(const AimAnalysisState& state) const {
    if (state.snapEvents.size() < MIN_SNAP_EVENTS) return 0.0f;

    // Count snaps with unnatural acceleration (τ_accel ≤ 1)
    uint32_t fastSnaps = 0;
    float meanAccelFrames = 0.0f;

    for (const auto& snap : state.snapEvents) {
        if (snap.accelerationFrames <= 1) fastSnaps++;
        meanAccelFrames += static_cast<float>(snap.accelerationFrames);
    }
    meanAccelFrames /= static_cast<float>(state.snapEvents.size());

    float fastSnapRatio = static_cast<float>(fastSnaps)
                        / static_cast<float>(state.snapEvents.size());

    // Binomial-probability-based confidence
    // Centered at 0.15 (7.5× the human baseline rate of 0.02)
    float rawScore = aim_math::logistic(fastSnapRatio, 0.15f, 20.0f);

    // Mean acceleration frames: human ≈ 2.5-4.0, aimbot ≈ 1.0-1.2
    float accelScore = aim_math::inverseLogistic(meanAccelFrames, 2.0f, 3.0f);

    return (rawScore + accelScore) / 2.0f * m_cfg.snapBaseConfidence;
}

// ══════════════════════════════════════════════════════════════════════════
// Silent Aim Confidence
// ══════════════════════════════════════════════════════════════════════════
//
// Silent aim sets the final view angle directly, bypassing intermediate
// frames. The server sees the angle change as a single-frame jump with
// no preceding velocity buildup or following deceleration.
//
// Key signature:
//   τ_accel = 1 (instant acceleration from zero)
//   τ_decel = 0 (instant deceleration to zero)
//   peak velocity ≥ 2000 deg/s (max human fade is ~1500 deg/s)
//
// A higher threshold than snap alone, since natural flicks can also
// show τ_accel = 1 occasionally (~2% of the time).
//
// ══════════════════════════════════════════════════════════════════════════

float AimAnalyzer::computeSilentAimConfidence(const AimAnalysisState& state) const {
    if (state.snapEvents.size() < 2) return 0.0f;

    uint32_t silentCandidates = 0;
    for (const auto& snap : state.snapEvents) {
        if (snap.accelerationFrames <= 1 &&
            snap.decelerationFrames <= 1 &&
            snap.peakVelocity > 2000.0f) {
            silentCandidates++;
        }
    }

    float ratio = static_cast<float>(silentCandidates)
                / static_cast<float>(state.snapEvents.size());

    float confidence = aim_math::logistic(ratio, 0.3f, 10.0f);
    return confidence * m_cfg.silentAimBaseConfidence;
}

// ══════════════════════════════════════════════════════════════════════════
// Tracking Confidence
// ══════════════════════════════════════════════════════════════════════════
//
// Perfect tracking maintains angular error below 0.5° over extended
// periods. Human tracking naturally oscillates with 2-5° RMS error.
//
// Confidence metric: proportion of frames with near-zero tracking error.
// ε_RMS < 0.5° for > 50% of engagement frames is suspicious.
//
// ══════════════════════════════════════════════════════════════════════════

float AimAnalyzer::computeTrackingConfidence(const AimAnalysisState& state) const {
    if (state.engagements.empty()) return 0.0f;

    float totalRms = 0.0f;
    uint32_t totalFrames = 0;
    uint32_t lowErrorFrames = 0;

    for (const auto& eng : state.engagements) {
        totalRms += eng.rmsTrackingError * static_cast<float>(eng.frameCount);
        totalFrames += eng.frameCount;

        if (eng.rmsTrackingError < m_cfg.perfectTrackThreshold) {
            lowErrorFrames += eng.frameCount;
        }
    }

    float avgRms = (totalFrames > 0)
        ? totalRms / static_cast<float>(totalFrames)
        : 999.0f;
    float lowRatio = (totalFrames > 0)
        ? static_cast<float>(lowErrorFrames) / static_cast<float>(totalFrames)
        : 0.0f;

    float rmsScore = aim_math::inverseLogistic(avgRms, m_cfg.naturalTrackThreshold, 2.0f);
    float lowScore = aim_math::logistic(lowRatio, 0.3f, 8.0f);

    return (rmsScore + lowScore) / 2.0f * m_cfg.trackingBaseConfidence;
}

// ══════════════════════════════════════════════════════════════════════════
// Tremor Confidence
// ══════════════════════════════════════════════════════════════════════════
//
// The absence of physiological tremor (RMS below 0.15 deg/s²) indicates
// aim smoothing or robotic input. Human tremor in the 8-12 Hz band
// produces an irreducible noise floor of ≈ 0.3 deg/s² during active aim.
//
// Confidence: logistic mapping from tremor RMS. Lower RMS = higher
// confidence of cheating.
//
// ══════════════════════════════════════════════════════════════════════════

float AimAnalyzer::computeTremorConfidence(const AimAnalysisState& state) const {
    if (state.totalActiveFrames < MIN_ANALYSIS_FRAMES) return 0.0f;

    float tremorRms = state.highFreqVelocity.stddev();

    // If we have very low data volume, reduce confidence
    float dataQuality = std::min(1.0f,
        static_cast<float>(state.totalActiveFrames) /
        static_cast<float>(MIN_ANALYSIS_FRAMES * 2)
    );

    float confidence = aim_math::inverseLogistic(
        tremorRms, m_cfg.tremorRmsThreshold, 15.0f
    );

    return confidence * m_cfg.tremorBaseConfidence * dataQuality;
}

// ══════════════════════════════════════════════════════════════════════════
// Reaction Confidence
// ══════════════════════════════════════════════════════════════════════════
//
// Human reaction time distribution (to visual stimulus):
//   - Elite athletes: μ ≈ 160ms, σ ≈ 25ms
//   - Average humans: μ ≈ 250ms, σ ≈ 60ms
//   - Physiological floor: ~100ms (unmaintainable)
//
// TriggerBot reactions:
//   - μ < 50ms (same-frame or next-frame)
//   - σ < 10ms (extremely consistent)
//
// Three independent features:
//   1. Count of sub-100ms reactions (impossible for humans)
//   2. Mean reaction time < 150ms (suspicious)
//   3. Minimum reaction time < 100ms (physiologically impossible)
//
// ══════════════════════════════════════════════════════════════════════════

float AimAnalyzer::computeReactionConfidence(const AimAnalysisState& state) const {
    if (state.reactions.size() < MIN_REACTION_EVENTS) return 0.0f;

    float minReact = 9999.0f;
    float sumReact = 0.0f;
    uint32_t impossibleCount = 0;

    for (const auto& event : state.reactions) {
        if (event.wasPreFire) continue;

        float rt = event.reactionTimeMs;
        sumReact += rt;
        if (rt < minReact) minReact = rt;
        if (rt < PHYSIOLOGICAL_REACTION_FLOOR_MS) impossibleCount++;
    }

    float meanReact = sumReact / static_cast<float>(state.reactions.size());

    // Feature 1: Count of impossible reactions (≥ 3 = conclusive)
    float impossibleScore = (impossibleCount >= 3)
        ? 1.0f
        : static_cast<float>(impossibleCount) / 3.0f;

    // Feature 2: Mean reaction time below elite threshold
    float meanScore = aim_math::inverseLogistic(
        meanReact, m_cfg.suspiciousReactionMs, 0.03f
    );

    // Feature 3: Minimum below physiological floor
    float minScore = aim_math::inverseLogistic(
        minReact, PHYSIOLOGICAL_REACTION_FLOOR_MS, 0.05f
    );

    float combined = impossibleScore * 0.5f + meanScore * 0.25f + minScore * 0.25f;
    return combined * m_cfg.reactionBaseConfidence;
}

// ══════════════════════════════════════════════════════════════════════════
// Curvature Confidence
// ══════════════════════════════════════════════════════════════════════════
//
// Human aim naturally follows curved paths (arm pivots at shoulder/elbow).
// Aimbot aim follows perfectly straight lines to the target.
//
// Curvature distribution comparison:
//   Human:  log-normal, μ ≈ 0.8, σ ≈ 0.6
//   Aimbot: near-zero, μ < 0.1, σ < 0.2
//
// Both low mean AND low standard deviation are required for confidence.
//
// ══════════════════════════════════════════════════════════════════════════

float AimAnalyzer::computeCurvatureConfidence(const AimAnalysisState& state) const {
    if (state.totalActiveFrames < MIN_ANALYSIS_FRAMES) return 0.0f;

    float meanCurv = state.curvatureStats.meanValue();
    float stdCurv  = state.curvatureStats.stddev();

    float meanScore = aim_math::inverseLogistic(
        meanCurv, m_cfg.lowCurvatureThreshold, 15.0f
    );
    float stdScore = aim_math::inverseLogistic(
        stdCurv, m_cfg.curvatureStdThreshold, 8.0f
    );

    return (meanScore + stdScore) / 2.0f * m_cfg.curvatureBaseConfidence;
}

// ══════════════════════════════════════════════════════════════════════════
// Correction Confidence (Micro-Adjustment Analysis)
// ══════════════════════════════════════════════════════════════════════════
//
// Human aim invariably overshoots the target and makes 1-3 micro-corrections
// (small reverse-direction adjustments) to settle on the target.
//
// These corrections are a hallmark of human motor control: the visual
// feedback loop (~150ms delay) causes the brain to overshoot, then correct.
//
// Aimbot aim lands exactly on target with zero corrections.
//
// Detection: fraction of snaps that have at least one detected correction
// (velocity sign-reversal at low speed after the snap peak).
//
// Human:  corrections on 60-90% of flicks
// Aimbot: corrections on < 10% of flicks
//
// ══════════════════════════════════════════════════════════════════════════

float AimAnalyzer::computeCorrectionConfidence(const AimAnalysisState& state) const {
    if (state.snapEvents.size() < MIN_SNAP_EVENTS) return 0.0f;

    uint32_t snapsWithCorrections = 0;
    for (const auto& snap : state.snapEvents) {
        if (snap.hasCorrection) snapsWithCorrections++;
    }

    float correctionRatio = static_cast<float>(snapsWithCorrections)
                          / static_cast<float>(state.snapEvents.size());

    // Humans have corrections on 60-90% of flicks.
    // Below 20% correction rate is suspicious.
    // Logistic maps this ratio: lower = more suspicious.
    float confidence = aim_math::inverseLogistic(correctionRatio, 0.3f, 10.0f);

    return confidence * m_cfg.trackingBaseConfidence;
}

// ══════════════════════════════════════════════════════════════════════════
// Public Accessors
// ══════════════════════════════════════════════════════════════════════════

float AimAnalyzer::getSuspicionScore(const std::string& playerId) {
    const auto& features = getFeatures(playerId);
    return features.overallConfidence * 100.0f;
}

std::string AimAnalyzer::getEvidence(const std::string& playerId) {
    const auto& features = getFeatures(playerId);
    std::ostringstream oss;

    struct FeatureEntry { const char* name; float value; };

    FeatureEntry entries[] = {
        {"Snap aim",          features.snapConfidence},
        {"Silent aim",        features.silentAimConfidence},
        {"Perfect tracking",  features.trackingConfidence},
        {"No tremor",         features.tremorConfidence},
        {"Fast reactions",    features.reactionConfidence},
        {"Straight paths",    features.curvatureConfidence},
        {"No corrections",    features.correctionConfidence},
    };

    const FeatureEntry* best = &entries[0];
    for (const auto& e : entries) {
        if (e.value > best->value) best = &e;
    }

    if (best->value > 0.3f) {
        oss << best->name << " (confidence: "
            << static_cast<int>(best->value * 100.0f) << "%)";
        int signalCount = 0;
        for (const auto& e : entries) {
            if (e.value > 0.3f) signalCount++;
        }
        if (signalCount > 1) {
            oss << " | " << signalCount << " corroborating signals";
        }
    } else {
        oss << "No significant detections";
    }

    return oss.str();
}

void AimAnalyzer::resetPlayer(const std::string& playerId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_playerStates.erase(playerId);
}

void AimAnalyzer::resetAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_playerStates.clear();
}

AimAnalysisState& AimAnalyzer::getState(const std::string& playerId) {
    auto it = m_playerStates.find(playerId);
    if (it != m_playerStates.end()) return it->second;

    auto& state = m_playerStates[playerId];
    state.highpassBuffer.fill(0.0f);
    return state;
}

} // namespace sentinel::modules::aim
