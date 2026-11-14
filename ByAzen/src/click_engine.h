// ============================================================================
// ByAzen click engine — timing logic of the macro worker.
//
// This header has no Windows dependencies on purpose: the worker thread in
// byazen_native.cpp drives it through a small Io adapter (clock, sleep, input),
// and tests/click_engine_test.cpp drives the very same code with a simulated
// clock, so the pacing can be checked without a Windows machine.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace clickengine {

inline constexpr int    kMinCps            = 5;
inline constexpr int    kMaxCps            = 1000;
inline constexpr int    kBurstFloorCps     = 200;   // burst never clicks slower than this
inline constexpr double kBurstSeconds      = 0.35;
inline constexpr double kRateWindowSeconds = 0.5;   // window of the live CPS readout

struct EngineSettings {
    int  targetCps     = 48;
    int  holdMs        = 3;
    int  jitterPct     = 0;
    bool pingSync      = true;
    int  pingMs        = 42;
    bool burstEnabled  = true;
    bool preciseTiming = false;
};

// What the OS timer can do. Waits shorter than minSleepMs are busy-waited;
// in spin mode the last slackMs before a deadline are busy-waited as well.
struct WaitCaps {
    double minSleepMs        = 0.5;
    double slackMs           = 1.0;
    double spinBelowPeriodMs = 4.0;  // cycles shorter than this always spin
};

struct CyclePlan {
    double periodMs;  // press-to-press interval
    double holdMs;    // press-to-release
    double minGapMs;  // shortest allowed release-to-press
};

// The target CPS has priority: a hold time that does not fit into the cycle is
// shortened instead of silently lowering the click rate.
inline CyclePlan planCycle(const EngineSettings& s, bool burstActive) {
    int cps = std::clamp(s.targetCps, kMinCps, kMaxCps);
    if (burstActive) cps = std::max(cps, kBurstFloorCps);
    double periodMs = 1000.0 / static_cast<double>(cps);
    if (s.pingSync) {
        // Same offset as before, limited to a quarter of the cycle so that it
        // cannot swallow the whole period at high click rates.
        const double offset = std::clamp((45.0 - s.pingMs) * 0.04, -3.0, 4.0);
        const double limit  = periodMs * 0.25;
        periodMs += std::clamp(offset, -limit, limit);
    }
    CyclePlan plan;
    plan.periodMs = periodMs;
    plan.holdMs   = std::min(static_cast<double>(std::max(1, s.holdMs)), periodMs * 0.6);
    plan.minGapMs = std::min(1.0, periodMs * 0.2);
    return plan;
}

// Keeps presses on an absolute time grid. A late wake-up shortens the next
// interval instead of shifting every later click, so the average rate equals
// the target. After a stall longer than one period the grid is re-anchored:
// missed clicks are dropped, never replayed as a catch-up burst.
class PressScheduler {
public:
    void reset() { started_ = false; }

    void onPress(double now, double periodSec) {
        if (!started_ || now - ideal_ > periodSec) {
            ideal_ = now;
            started_ = true;
        }
    }

    double scheduleNext(double releasedAt, double intervalSec, double minGapSec) {
        ideal_ += intervalSec;
        return std::max(ideal_, releasedAt + minGapSec);
    }

private:
    double ideal_   = 0.0;
    bool   started_ = false;
};

// Live click rate, computed from the spacing of recent clicks. Unlike a
// "clicks during the last second" counter it is correct from the second click
// on, instead of climbing for a whole second after the trigger is pressed.
class RateMeter {
public:
    void reset() { times_.clear(); }

    void add(double now) {
        times_.push_back(now);
        std::size_t stale = 0;
        while (stale < times_.size() && now - times_[stale] > kRateWindowSeconds) ++stale;
        if (stale > 0) times_.erase(times_.begin(), times_.begin() + static_cast<std::ptrdiff_t>(stale));
    }

    // 0 until two clicks have been seen.
    int rate() const {
        if (times_.size() < 2) return 0;
        const double span = times_.back() - times_.front();
        if (span <= 0.0) return 0;
        return static_cast<int>(std::lround(static_cast<double>(times_.size() - 1) / span));
    }

private:
    std::vector<double> times_;
};

// Io must provide: double now(); void sleepMs(double); void pause().
template <class Io>
void waitUntil(Io& io, double deadline, bool spin, const WaitCaps& caps) {
    for (;;) {
        const double remainingMs = (deadline - io.now()) * 1000.0;
        if (remainingMs <= 0.0) return;
        const double sleepMs = spin ? remainingMs - caps.slackMs : remainingMs;
        if (sleepMs >= caps.minSleepMs) {
            io.sleepMs(sleepMs);
            continue;  // re-check: the OS timer may return a little early or late
        }
        while (io.now() < deadline) io.pause();
        return;
    }
}

class ClickEngine {
public:
    explicit ClickEngine(const WaitCaps& caps) : caps_(caps) {}

    bool firing() const { return firing_; }
    int  measuredCps() const { return meter_.rate(); }

    void stop() {
        firing_ = false;
        meter_.reset();
    }

    // One click: press now, release after the hold time, then wait until the
    // next press is due. The very first click after stop() goes out immediately.
    // Io must also provide: void press(); void release().
    // jitterUnit is a uniform random number in [-1, 1].
    template <class Io>
    void cycle(Io& io, const EngineSettings& s, double jitterUnit) {
        const double now = io.now();
        if (!firing_) {
            firing_ = true;
            burstUntil_ = s.burstEnabled ? now + kBurstSeconds : now;
            sched_.reset();
            meter_.reset();
        }
        const bool burstActive = s.burstEnabled && now < burstUntil_;
        const CyclePlan plan = planCycle(s, burstActive);
        const bool spin = s.preciseTiming || plan.periodMs < caps_.spinBelowPeriodMs;

        sched_.onPress(now, plan.periodMs / 1000.0);
        io.press();
        waitUntil(io, now + plan.holdMs / 1000.0, spin, caps_);
        io.release();

        const double released = io.now();
        meter_.add(released);

        const double jitterPct  = static_cast<double>(std::clamp(s.jitterPct, 0, 20));
        const double jitterMs   = (plan.periodMs - plan.holdMs) * (jitterPct / 100.0) * std::clamp(jitterUnit, -1.0, 1.0);
        const double intervalMs = plan.periodMs + jitterMs;
        const double nextPress  = sched_.scheduleNext(released, intervalMs / 1000.0, plan.minGapMs / 1000.0);
        waitUntil(io, nextPress, spin, caps_);
    }

private:
    WaitCaps       caps_;
    PressScheduler sched_;
    RateMeter      meter_;
    bool           firing_     = false;
    double         burstUntil_ = 0.0;
};

} // namespace clickengine
