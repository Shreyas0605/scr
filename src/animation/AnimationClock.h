#pragma once
#include <chrono>
#include <cstdint>

namespace fcs::animation {

// Wraps QueryPerformanceCounter semantics via std::chrono::steady_clock to
// provide sub-millisecond precision timing for the render/animation loop.
// Frame pacing and flip-duration calculations are all derived from this
// clock rather than GetTickCount, avoiding drift over long uptimes.
class AnimationClock {
public:
    AnimationClock() { Reset(); }

    void Reset() {
        m_start = Clock::now();
        m_last = m_start;
    }

    // Returns seconds elapsed since the previous call to Tick() (or since
    // construction/Reset() on the first call).
    double Tick() {
        const auto now = Clock::now();
        const double dt = std::chrono::duration<double>(now - m_last).count();
        m_last = now;
        return dt;
    }

    // Total seconds elapsed since construction/Reset(), independent of Tick().
    double TotalSeconds() const {
        return std::chrono::duration<double>(Clock::now() - m_start).count();
    }

    // Monotonic timestamp in seconds, suitable for scheduling absolute
    // animation start/end times.
    static double NowSeconds() {
        return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_start;
    Clock::time_point m_last;
};

// Represents a single timed transition (e.g. one flip-tile animation or
// one background crossfade). Progress() returns eased [0,1] using the
// supplied easing function pointer.
class Transition {
public:
    Transition() = default;

    void Start(double durationSeconds, double nowSeconds) {
        m_startTime = nowSeconds;
        m_duration = durationSeconds > 0.0 ? durationSeconds : 0.0001;
        m_active = true;
    }

    // Linear (un-eased) progress in [0,1]; caller applies its own easing.
    float LinearProgress(double nowSeconds) const {
        if (!m_active) return 1.0f;
        const double t = (nowSeconds - m_startTime) / m_duration;
        if (t >= 1.0) return 1.0f;
        if (t <= 0.0) return 0.0f;
        return static_cast<float>(t);
    }

    bool IsFinished(double nowSeconds) const {
        return (nowSeconds - m_startTime) >= m_duration;
    }

    bool IsActive() const { return m_active; }
    void Stop() { m_active = false; }

private:
    double m_startTime = 0.0;
    double m_duration = 0.0;
    bool m_active = false;
};

} // namespace fcs::animation
