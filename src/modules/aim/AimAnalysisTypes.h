// ============================================================================
// Sentinel AntiCheat - Human Aim Analysis Type Definitions
// ============================================================================
// Mathematical types, circular buffer, online statistics, and feature vectors
// for statistical analysis of player aim behavior.
// ============================================================================

#pragma once
#include "core/Types.h"
#include <cmath>
#include <deque>
#include <numeric>
#include <array>
#include <cstdint>

namespace sentinel::modules::aim {

// ══════════════════════════════════════════════════════════════════════════
// Constants
// ══════════════════════════════════════════════════════════════════════════

/// Maximum number of angular samples to retain for analysis.
/// At 64 Hz server tick, 512 samples = 8 seconds of history.
inline constexpr size_t MAX_ANGLE_SAMPLES  = 512;

/// Maximum number of reaction events to track.
inline constexpr size_t MAX_REACTION_EVENTS = 32;

/// Maximum number of tracking engagement samples.
inline constexpr size_t MAX_TRACKING_SAMPLES = 256;

/// Minimum angular displacement to consider as "active aim" (degrees).
/// Below this, the player is likely not actively aiming.
inline constexpr float MIN_ACTIVE_AIM_DELTA = 0.01f;

/// Minimum number of active frames before analysis is meaningful.
inline constexpr size_t MIN_ANALYSIS_FRAMES = 30;

/// Minimum snap events before snap analysis is meaningful.
inline constexpr size_t MIN_SNAP_EVENTS = 3;

/// Minimum reaction events before reaction analysis is meaningful.
inline constexpr size_t MIN_REACTION_EVENTS = 3;

/// Minimum tracking frames before tracking analysis is meaningful.
inline constexpr size_t MIN_TRACKING_FRAMES = 30;

/// Physiological tremor frequency range (Hz).
inline constexpr float TREMOR_FREQ_LOW  = 6.0f;
inline constexpr float TREMOR_FREQ_HIGH = 14.0f;

/// Human reaction time floor (milliseconds).
/// Olympic-level reaction time is ~100ms; below this is physiologically impossible
/// for sustained performance.
inline constexpr float PHYSIOLOGICAL_REACTION_FLOOR_MS = 100.0f;

/// Degrees per second — threshold for "snap" classification at 64 Hz frame rate.
/// At 64 Hz, a single-frame 45-degree change = 2880 deg/s. Human flicks max
/// at roughly 1000-2000 deg/s depending on sensitivity.
inline constexpr float SNAP_VELOCITY_THRESHOLD_DEG_S = 1500.0f;

/// Degrees — minimum aim displacement to classify as a "flick" rather than
/// micro-adjustment.
inline constexpr float MIN_FLICK_DISPLACEMENT_DEG = 10.0f;

// ══════════════════════════════════════════════════════════════════════════
// Online Statistics (Welford's Algorithm)
// ══════════════════════════════════════════════════════════════════════════

/// Online mean and variance using Welford's numerically stable algorithm.
/// O(1) per update, O(1) memory.
/// Reference: Welford, B. P. (1962). "Note on a method for calculating
/// corrected sums of squares and products." Technometrics, 4(3), 419-420.
class OnlineStatistics {
public:
    void push(float x) noexcept {
        ++n;
        float delta = x - mean;
        mean += delta / static_cast<float>(n);
        float delta2 = x - mean;
        m2 += delta * delta2;
    }

    [[nodiscard]] float count()  const noexcept { return static_cast<float>(n); }
    [[nodiscard]] float meanValue() const noexcept { return mean; }
    [[nodiscard]] float variance() const noexcept {
        return (n > 1) ? m2 / static_cast<float>(n - 1) : 0.0f;
    }
    [[nodiscard]] float stddev() const noexcept { return std::sqrt(variance()); }
    [[nodiscard]] float cv() const noexcept {  // Coefficient of variation
        return (mean > 1e-6f) ? stddev() / mean : 0.0f;
    }

    void reset() noexcept {
        n = 0; mean = 0.0f; m2 = 0.0f;
    }

private:
    size_t n = 0;
    float mean = 0.0f;
    float m2 = 0.0f;  // Sum of squared differences
};

// ══════════════════════════════════════════════════════════════════════════
// Ring Buffer (fixed-size circular buffer)
// ══════════════════════════════════════════════════════════════════════════

/// Fixed-capacity circular buffer for angular samples.
/// O(1) push, O(1) random access. Automatically evicts oldest elements.
template<typename T, size_t Capacity>
class RingBuffer {
public:
    static_assert(Capacity > 0, "RingBuffer capacity must be positive");

    void push_back(const T& value) {
        m_data[m_head] = value;
        m_head = (m_head + 1) % Capacity;
        if (m_size < Capacity) ++m_size;
    }

    [[nodiscard]] size_t size() const noexcept { return m_size; }
    [[nodiscard]] size_t capacity() const noexcept { return Capacity; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] bool full() const noexcept { return m_size == Capacity; }

    /// Access element by index (0 = oldest, size()-1 = newest)
    [[nodiscard]] const T& operator[](size_t index) const {
        // index 0 = oldest = head if not wrapped, or head (which points to oldest+1 area)
        // Let's re-derive: 
        // If size < Capacity, data is at [0, head) in insertion order
        // If size == Capacity, data starts at head and wraps around
        size_t idx;
        if (m_size < Capacity) {
            idx = index;
        } else {
            idx = (m_head + index) % Capacity;
        }
        return m_data[idx];
    }

    [[nodiscard]] const T& newest() const { return (*this)[m_size - 1]; }
    [[nodiscard]] const T& oldest() const { return (*this)[0]; }

    void clear() { m_size = 0; m_head = 0; }

private:
    std::array<T, Capacity> m_data{};
    size_t m_head = 0;
    size_t m_size = 0;
};

// ══════════════════════════════════════════════════════════════════════════
// Angular Sample
// ══════════════════════════════════════════════════════════════════════════

/// A single frame's angular measurement with metadata.
struct AimSample {
    QAngle   angles;          // Player's view angles at this sample
    QAngle   punchAngles;     // Recoil punch (weapon kick)
    float    deltaTime   = 0.0f;   // Seconds since previous sample
    float    angularVelocity  = 0.0f;  // deg/s (computed)
    float    angularAccel     = 0.0f;  // deg/s² (computed)
    float    curvature        = 0.0f;  // Discrete curvature (computed)
    bool     isFiring     = false; // Player is attacking this frame
    bool     isMoving     = false; // Player is moving (velocity > threshold)
    uint32_t serverTick  = 0;
};

// ══════════════════════════════════════════════════════════════════════════
// Snap Event
// ══════════════════════════════════════════════════════════════════════════

/// A detected snap/flick event with profile analysis.
struct SnapEvent {
    float    peakVelocity    = 0.0f;  // deg/s
    float    totalDisplacement = 0.0f;  // degrees
    uint32_t accelerationFrames = 0;   // Frames from 10% to 90% peak
    uint32_t decelerationFrames = 0;   // Frames from peak to 10%  
    uint32_t totalFrames     = 0;      // Total duration of flick
    float    landingError    = 999.0f; // Angular distance to nearest enemy at landing (if available)
    bool     targetedEnemy   = false;  // Did the snap land on an enemy?
    float    overshoot       = 0.0f;   // Overshoot past target (degrees)
    bool     hasCorrection   = false;  // Was there a corrective micro-adjustment after landing?
    float    maxAngularAccel = 0.0f;   // deg/s²
};

// ══════════════════════════════════════════════════════════════════════════
// Engagement Event (Tracking Period)
// ══════════════════════════════════════════════════════════════════════════

/// A period of active combat engagement for tracking analysis.
struct EngagementEvent {
    uint32_t frameCount       = 0;    // Duration in frames
    float    duration         = 0.0f;  // Duration in seconds
    float    rmsTrackingError = 0.0f;  // Degrees RMS
    float    peakTrackingError = 0.0f; // Degrees peak
    float    correctionTime   = 0.0f;  // Time to correct 50% error (seconds)
    int      corrections      = 0;    // Number of over-correction oscillations
    bool     validLOS         = false; // Had line-of-sight confirmation
};

// ══════════════════════════════════════════════════════════════════════════
// Reaction Event
// ══════════════════════════════════════════════════════════════════════════

/// A reaction event: enemy becomes visible -> player fires.
struct ReactionEvent {
    float reactionTimeMs  = 0.0f;  // Milliseconds
    bool  wasPreFire      = false; // Player fired before enemy was visible (prefire = not a reaction)
    float enemyDistance   = 0.0f;  // Units
    bool  wasFirstBullet  = false; // First bullet accuracy (spray vs tap)
};

// ══════════════════════════════════════════════════════════════════════════
// Feature Vector (output of analysis)
// ══════════════════════════════════════════════════════════════════════════

/// Normalized feature scores for each detection dimension.
/// Each feature is a confidence in [0, 1] where:
/// 0.0 = completely natural human behavior
/// 1.0 = impossible for human, certain cheat
struct AimFeatureVector {
    // ── Primary Features ───────────────────────────────────────────────
    float snapConfidence       = 0.0f;  // Snap aim / impossible flicks
    float silentAimConfidence  = 0.0f;  // Instantaneous angle changes without intermediate frames
    float trackingConfidence   = 0.0f;  // Perfect tracking / no human error
    float tremorConfidence     = 0.0f;  // Absence of physiological tremor
    float reactionConfidence   = 0.0f;  // Impossible reaction times
    float curvatureConfidence  = 0.0f;  // Unnatural path curvature
    float correctionConfidence = 0.0f;  // Absence of correction micro-adjustments

    // ── Aggregate ──────────────────────────────────────────────────────
    float overallConfidence    = 0.0f;  // Weighted combination

    /// Compute aggregate confidence using evidence accumulation.
    void computeOverall() noexcept {
        // We do NOT simply average. We require MULTIPLE independent signals.
        // A single high-confidence feature is suspicious but not conclusive.
        // Two or more is very strong evidence.
        
        float signals[7] = {
            snapConfidence, silentAimConfidence, trackingConfidence,
            tremorConfidence, reactionConfidence, curvatureConfidence,
            correctionConfidence
        };

        // Count how many features exceed their individual thresholds
        int strongSignals = 0;
        float weightedSum = 0.0f;
        
        for (float s : signals) {
            if (s > 0.5f) strongSignals++;
            weightedSum += s;
        }

        // Evidence accumulation:
        // - 0 strong signals: overall confidence = 0
        // - 1 strong signal:  overall = that signal's confidence * 0.7 (doubt)
        // - 2+ strong signals: overall approaches 1 rapidly
        
        if (strongSignals == 0) {
            overallConfidence = weightedSum / 14.0f;  // Very low
        } else if (strongSignals == 1) {
            // Find the strongest signal and discount it
            float maxSig = 0.0f;
            for (float s : signals) if (s > maxSig) maxSig = s;
            overallConfidence = maxSig * 0.7f;
        } else {
            // Multiple corroborating signals = strong evidence
            // Logistic-like combination: 1 / (1 + e^(-k*(weightedSum - threshold)))
            float raw = weightedSum / 3.0f - 1.0f;  // Center around 3 strong signals
            overallConfidence = 1.0f / (1.0f + std::exp(-3.0f * raw));
        }
    }
};

// ══════════════════════════════════════════════════════════════════════════
// Per-Player Analysis State
// ══════════════════════════════════════════════════════════════════════════

/// Complete analysis state for one player.
struct AimAnalysisState {
    // ── Raw Data ───────────────────────────────────────────────────────
    RingBuffer<AimSample, MAX_ANGLE_SAMPLES> samples;
    
    // ── Angular Kinematics ─────────────────────────────────────────────
    OnlineStatistics velocityStats;       // ω distribution
    OnlineStatistics accelStats;          // α distribution
    OnlineStatistics curvatureStats;      // κ distribution
    OnlineStatistics highFreqVelocity;    // High-pass filtered ω (tremor band)

    // ── Snap/Flick Events ──────────────────────────────────────────────
    std::deque<SnapEvent> snapEvents;
    OnlineStatistics snapVelocityStats;   // Distribution of snap peak velocities
    OnlineStatistics snapAccelFramesStats; // Distribution of accel frame counts

    // ── Tracking ───────────────────────────────────────────────────────
    std::deque<EngagementEvent> engagements;
    OnlineStatistics trackingErrorStats;

    // ── Reaction ───────────────────────────────────────────────────────
    std::deque<ReactionEvent> reactions;
    OnlineStatistics reactionStats;

    // ── Detection State ────────────────────────────────────────────────
    AimFeatureVector currentFeatures;
    uint32_t totalFramesAnalyzed = 0;
    uint32_t totalActiveFrames   = 0;  // Frames with active aiming
    float    timeSinceLastSnap   = 0.0f;  // Cooldown timer
    float    timeSinceLastReaction = 0.0f;

    // ── Computed, Lazily Evaluated ─────────────────────────────────────
    float    movingAvgVelocity   = 0.0f;  // EMA of angular velocity
    float    movingAvgAccel      = 0.0f;
    float    tremorRms           = 0.0f;
    
    // Snap profile signature (0=human, 1=robotic)
    float    snapProfileScore    = 0.0f;

    // Computed kinematics for the current frame (set by computeKinematics)
    struct {
        float velocity  = 0.0f;  // deg/s
        float accel     = 0.0f;  // deg/s²
        float curvature = 0.0f;  // dimensionless
    } currentKinematics;

    // Ring buffer for high-pass filter (tremor analysis)
    std::array<float, 16> highpassBuffer{};
    size_t highpassIndex = 0;
    float  tremorEmaState = 0.0f;  // Per-player EMA state for HPF

    // Previous frame values for derivative computation
    QAngle   prevAngles;
    bool     hasPrevFrame = false;
    float    prevVelocity = 0.0f;
    QAngle   prevDelta;            // Δθ(t-1) for curvature
    bool     hasPrevDelta = false;

    // Last snap tracking
    bool     inSnapDecel  = false;  // Was the last frame part of a snap decel?
    float    snapStartTime = 0.0f; // Server time when current snap started
    int      framesSinceSnap = 999; // Frames elapsed since last snap peak

    // Enemy visibility tracking (for reaction time)
    bool     enemyVisible  = false;
    float    enemyVisibleTime = 0.0f; // Accumulated frame time when enemy visible
    Vector3  lastEnemyPos;
};

// ══════════════════════════════════════════════════════════════════════════
// Math Utilities
// ══════════════════════════════════════════════════════════════════════════

namespace aim_math {

/// Normalize angle to [-180, 180] range.
inline float normalizeAngle(float a) noexcept {
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

/// Angular difference between two angles (shortest path).
inline float angleDiff(float a, float b) noexcept {
    return normalizeAngle(a - b);
}

/// Magnitude of QAngle difference (degrees).
inline float angleDeltaMag(const QAngle& a, const QAngle& b) noexcept {
    float dp = angleDiff(a.pitch, b.pitch);
    float dy = angleDiff(a.yaw, b.yaw);
    return std::sqrt(dp * dp + dy * dy);
}

/// Convert QAngle to unit direction vector.
inline Vector3 angleToVector(const QAngle& angles) noexcept {
    float cp = std::cos(angles.pitch * 3.14159265f / 180.0f);
    float sp = std::sin(angles.pitch * 3.14159265f / 180.0f);
    float cy = std::cos(angles.yaw * 3.14159265f / 180.0f);
    float sy = std::sin(angles.yaw * 3.14159265f / 180.0f);
    return Vector3{cp * cy, cp * sy, -sp};
}

/// Exponential moving average: new = alpha * x + (1 - alpha) * old.
inline float ema(float old, float x, float alpha) noexcept {
    return alpha * x + (1.0f - alpha) * old;
}

/// Simple high-pass filter: y = x - ema(x).
/// Used for tremor extraction (removes low-frequency drift).
inline float highpass(float x, float& state, float alpha) noexcept {
    float low = ema(state, x, alpha);
    state = low;
    return x - low;
}

/// Soft threshold (logistic): smoothly maps value to [0, 1].
/// Center = center point, steepness = how abrupt the transition.
inline float logistic(float x, float center, float steepness) noexcept {
    return 1.0f / (1.0f + std::exp(-steepness * (x - center)));
}

/// Inverse logistic: maps high feature values to low confidence and vice versa.
inline float inverseLogistic(float x, float center, float steepness) noexcept {
    return 1.0f / (1.0f + std::exp(steepness * (x - center)));
}

/// Compute discrete curvature from three consecutive angle samples.
/// κ = |Δθ_i - Δθ_{i-1}| / (|Δθ_i| + |Δθ_{i-1}| + ε)
inline float discreteCurvature(const QAngle& a, const QAngle& b, const QAngle& c) noexcept {
    QAngle d1 = b - a;
    QAngle d2 = c - b;

    auto normAngle = [](QAngle& q) {
        q.pitch = normalizeAngle(q.pitch);
        q.yaw = normalizeAngle(q.yaw);
    };
    normAngle(d1);
    normAngle(d2);

    float mag1 = d1.length();
    float mag2 = d2.length();
    float diff = angleDeltaMag(d2, d1);

    constexpr float EPS = 0.001f;
    return diff / (mag1 + mag2 + EPS);
}

/// RMS of a deque of values.
inline float rms(const std::deque<float>& values) noexcept {
    if (values.empty()) return 0.0f;
    float sumSq = 0.0f;
    for (float v : values) sumSq += v * v;
    return std::sqrt(sumSq / static_cast<float>(values.size()));
}

} // namespace aim_math

} // namespace sentinel::modules::aim
