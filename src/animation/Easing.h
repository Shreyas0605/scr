#pragma once
#include <cmath>
#include <algorithm>

namespace fcs::animation {

// Standard easing functions operating on normalized time t in [0, 1].
// All functions clamp their input and return a value in [0, 1] (some,
// like Back/Elastic, may briefly overshoot which is intentional).
namespace Easing {

inline float Clamp01(float t) {
    return std::clamp(t, 0.0f, 1.0f);
}

inline float Linear(float t) {
    return Clamp01(t);
}

// Cubic ease-out: fast start, gentle settle. Used for the "falling" half
// of the flip animation so the tile decelerates like real mechanical
// weight settling into place.
inline float CubicOut(float t) {
    t = Clamp01(t);
    const float f = t - 1.0f;
    return f * f * f + 1.0f;
}

// Cubic ease-in: slow start, fast finish. Used for the initial lift of a
// flip tile leaving its resting position.
inline float CubicIn(float t) {
    t = Clamp01(t);
    return t * t * t;
}

// Ease-in-out cubic: symmetric acceleration/deceleration.
inline float CubicInOut(float t) {
    t = Clamp01(t);
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    }
    const float f = (2.0f * t) - 2.0f;
    return 0.5f * f * f * f + 1.0f;
}

// Quadratic ease-out.
inline float QuadOut(float t) {
    t = Clamp01(t);
    return t * (2.0f - t);
}

// Subtle overshoot at the end of the flip, giving the tile a very slight
// "settle bounce" reminiscent of real split-flap hardware without being
// cartoonish. Overshoot amount is intentionally small (c1 ~ 1.28).
inline float BackOut(float t) {
    t = Clamp01(t);
    constexpr float c1 = 1.28f;
    constexpr float c3 = c1 + 1.0f;
    const float f = t - 1.0f;
    return 1.0f + c3 * f * f * f + c1 * f * f;
}

// Sine-based ease-in-out, very smooth, used for background crossfades.
inline float SineInOut(float t) {
    t = Clamp01(t);
    return -0.5f * (std::cos(3.14159265358979323846f * t) - 1.0f);
}

} // namespace Easing
} // namespace fcs::animation
