// Off-Windows check of the click pacing. Drives src/click_engine.h — the same
// code the worker thread runs — with a simulated clock whose timer wakes up
// late by a random amount, like a real OS timer does.
//
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror tests/click_engine_test.cpp -o click_engine_test && ./click_engine_test

#include "../src/click_engine.h"

#include <cstdio>
#include <random>
#include <vector>

using namespace clickengine;

namespace {

int gFailures = 0;

void check(bool ok, const char* what, double got, double want) {
    if (ok) return;
    ++gFailures;
    std::printf("  FAIL %s: got %.4f, expected %.4f\n", what, got, want);
}

struct SimIo {
    double t = 0.0;
    double overshootMaxMs = 0.6;   // how late a timer sleep may return
    double inputCostMs    = 0.03;  // cost of one SendInput call
    double pauseCostMs    = 0.0002;
    double stallAtSec     = -1.0;  // inject one long stall at this time
    double stallMs        = 0.0;
    double spunMs         = 0.0;
    std::mt19937 rng{12345};
    std::vector<double> presses;
    std::vector<double> releases;

    double now() const { return t; }
    void sleepMs(double ms) {
        std::uniform_real_distribution<double> late(0.0, overshootMaxMs);
        t += (ms + late(rng)) / 1000.0;
        if (stallAtSec >= 0.0 && t >= stallAtSec) {
            t += stallMs / 1000.0;
            stallAtSec = -1.0;
        }
    }
    void pause() { t += pauseCostMs / 1000.0; spunMs += pauseCostMs; }
    void press() { presses.push_back(t); t += inputCostMs / 1000.0; }
    void release() { releases.push_back(t); t += inputCostMs / 1000.0; }
};

struct Run {
    SimIo io;
    std::vector<int> meterAfterClick;
};

Run simulate(const EngineSettings& s, const WaitCaps& caps, double seconds, SimIo io = SimIo{}) {
    Run run;
    run.io = io;
    ClickEngine engine(caps);
    std::mt19937 rng(777);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    while (run.io.t < seconds) {
        run.io.t += 0.00001;  // worker-loop overhead between clicks (settings read, key polling)
        engine.cycle(run.io, s, s.jitterPct > 0 ? unit(rng) : 0.0);
        run.meterAfterClick.push_back(engine.measuredCps());
    }
    return run;
}

double achievedCps(const std::vector<double>& presses, double fromSec = 0.0) {
    std::size_t first = 0;
    while (first < presses.size() && presses[first] < fromSec) ++first;
    if (presses.size() - first < 2) return 0.0;
    return static_cast<double>(presses.size() - first - 1) / (presses.back() - presses[first]);
}

EngineSettings plain(int cps) {
    EngineSettings s;
    s.targetCps = cps;
    s.burstEnabled = false;
    s.pingSync = false;
    return s;
}

const WaitCaps kHiRes{0.5, 1.0, 4.0};
const WaitCaps kLegacy{1.0, 2.0, 12.0};

void testRateMatchesTarget(const char* name, const WaitCaps& caps, double overshootMaxMs) {
    std::printf("[%s] achieved CPS vs target\n", name);
    for (int cps : {5, 26, 48, 100, 200, 250, 251, 333, 500, 750, 1000}) {
        SimIo io;
        io.overshootMaxMs = overshootMaxMs;
        const Run run = simulate(plain(cps), caps, 4.0, io);
        const double got = achievedCps(run.io.presses);
        std::printf("  target %4d -> %8.2f CPS, busy-wait %4.1f%% of one core\n",
                    cps, got, run.io.spunMs / 4000.0 * 100.0);
        check(std::fabs(got - cps) <= cps * 0.005, "average rate", got, cps);
        const CyclePlan plan = planCycle(plain(cps), false);
        for (std::size_t i = 0; i + 1 < run.io.presses.size(); ++i) {
            const double holdMs = (run.io.releases[i] - run.io.presses[i]) * 1000.0;
            const double gapMs  = (run.io.presses[i + 1] - run.io.releases[i]) * 1000.0;
            if (holdMs < plan.holdMs - 0.001) { check(false, "hold too short", holdMs, plan.holdMs); break; }
            if (gapMs < plan.minGapMs - 0.001) { check(false, "release-to-press gap too short", gapMs, plan.minGapMs); break; }
        }
    }
}

void testInstantStart() {
    std::printf("[start] the first clicks already run at full speed\n");
    for (int cps : {48, 200, 500, 1000}) {
        const Run run = simulate(plain(cps), kHiRes, 1.0);
        const double periodMs = 1000.0 / cps;
        const double firstPressMs = run.io.presses[0] * 1000.0;
        const double secondGapMs  = (run.io.presses[1] - run.io.presses[0]) * 1000.0;
        std::printf("  target %4d: first press after %.3f ms, second after %.3f ms, readout after 2 clicks: %d CPS\n",
                    cps, firstPressMs, secondGapMs, run.meterAfterClick[1]);
        check(firstPressMs < 0.05, "first press is immediate", firstPressMs, 0.0);
        check(secondGapMs <= periodMs + 0.7, "second press on time", secondGapMs, periodMs);
        check(run.meterAfterClick[0] == 0, "readout needs two clicks", run.meterAfterClick[0], 0);
        // Three clicks in: the readout must already be at the target, not climbing for a second.
        check(std::fabs(run.meterAfterClick[2] - cps) <= cps * 0.12 + 1, "readout after 3 clicks", run.meterAfterClick[2], cps);
        check(std::fabs(run.meterAfterClick.back() - cps) <= cps * 0.02 + 1, "steady readout", run.meterAfterClick.back(), cps);
    }
}

void testBurst() {
    std::printf("[burst] start boost never clicks slower than the target\n");
    EngineSettings low = plain(48);
    low.burstEnabled = true;
    const Run a = simulate(low, kHiRes, 3.0);
    int during = 0;
    for (double p : a.io.presses) during += (p < kBurstSeconds) ? 1 : 0;
    const double after = achievedCps(a.io.presses, kBurstSeconds + 0.1);
    std::printf("  target 48: %d clicks in the first %.0f ms, then %.2f CPS\n", during, kBurstSeconds * 1000.0, after);
    check(std::abs(during - 70) <= 2, "burst click count", during, 70);
    check(std::fabs(after - 48.0) <= 0.5, "rate after burst", after, 48.0);

    EngineSettings high = plain(500);
    high.burstEnabled = true;
    const Run b = simulate(high, kHiRes, 2.0);
    int burstClicks = 0;
    for (double p : b.io.presses) burstClicks += (p < kBurstSeconds) ? 1 : 0;
    std::printf("  target 500: %d clicks in the first %.0f ms\n", burstClicks, kBurstSeconds * 1000.0);
    check(std::abs(burstClicks - 175) <= 3, "burst keeps a higher target", burstClicks, 175);
}

void testStallRecovery() {
    std::printf("[stall] a frozen system does not cause a catch-up burst\n");
    SimIo io;
    io.stallAtSec = 1.0;
    io.stallMs = 120.0;
    const Run run = simulate(plain(100), kHiRes, 3.0, io);
    double minIntervalMs = 1e9;
    double maxIntervalMs = 0.0;
    for (std::size_t i = 0; i + 1 < run.io.presses.size(); ++i) {
        const double d = (run.io.presses[i + 1] - run.io.presses[i]) * 1000.0;
        minIntervalMs = std::min(minIntervalMs, d);
        maxIntervalMs = std::max(maxIntervalMs, d);
    }
    const double after = achievedCps(run.io.presses, 1.3);
    std::printf("  target 100: longest interval %.1f ms, shortest %.2f ms, rate afterwards %.2f CPS\n",
                maxIntervalMs, minIntervalMs, after);
    check(maxIntervalMs > 100.0, "stall happened", maxIntervalMs, 120.0);
    check(minIntervalMs > 8.0, "no catch-up burst", minIntervalMs, 10.0);
    check(std::fabs(after - 100.0) <= 0.5, "rate after stall", after, 100.0);
}

void testJitterAndPing() {
    std::printf("[jitter/ping]\n");
    EngineSettings j = plain(48);
    j.jitterPct = 20;
    const Run run = simulate(j, kHiRes, 20.0);
    const double got = achievedCps(run.io.presses);
    double minMs = 1e9, maxMs = 0.0;
    for (std::size_t i = 0; i + 1 < run.io.presses.size(); ++i) {
        const double d = (run.io.presses[i + 1] - run.io.presses[i]) * 1000.0;
        minMs = std::min(minMs, d);
        maxMs = std::max(maxMs, d);
    }
    std::printf("  48 CPS, jitter 20%%: average %.2f CPS, intervals %.1f..%.1f ms\n", got, minMs, maxMs);
    check(std::fabs(got - 48.0) <= 1.0, "jitter keeps the average", got, 48.0);
    check(maxMs - minMs > 3.0, "jitter varies the interval", maxMs - minMs, 7.0);

    EngineSettings p = plain(48);
    p.pingSync = true;
    p.pingMs = 42;
    const CyclePlan lowRate = planCycle(p, false);
    // Old engine: hold 3 ms + pause (1000/48 - 3 + 0.12) ms.
    check(std::fabs(lowRate.periodMs - (1000.0 / 48.0 + 0.12)) < 1e-9, "ping sync unchanged at 48 CPS", lowRate.periodMs, 1000.0 / 48.0 + 0.12);
    check(lowRate.holdMs == 3.0, "hold unchanged at 48 CPS", lowRate.holdMs, 3.0);

    p.targetCps = 1000;
    p.pingMs = 0;
    const CyclePlan highRate = planCycle(p, false);
    std::printf("  1000 CPS, ping sync at 0 ms ping: period %.3f ms, hold %.3f ms\n", highRate.periodMs, highRate.holdMs);
    check(highRate.periodMs <= 1.25 + 1e-9, "ping sync bounded at 1000 CPS", highRate.periodMs, 1.25);
    check(highRate.holdMs < highRate.periodMs, "hold fits into the cycle", highRate.holdMs, highRate.periodMs);

    EngineSettings wide = plain(5000);
    check(std::fabs(planCycle(wide, false).periodMs - 1.0) < 1e-9, "CPS clamped to the maximum", planCycle(wide, false).periodMs, 1.0);
    wide.targetCps = 0;
    check(std::fabs(planCycle(wide, false).periodMs - 200.0) < 1e-9, "CPS clamped to the minimum", planCycle(wide, false).periodMs, 200.0);
}

} // namespace

int main() {
    testRateMatchesTarget("high-resolution timer", kHiRes, 0.6);
    testRateMatchesTarget("legacy 1 ms timer", kLegacy, 1.5);
    testInstantStart();
    testBurst();
    testStallRecovery();
    testJitterAndPing();
    if (gFailures == 0) std::printf("\nALL CHECKS PASSED\n");
    else std::printf("\n%d CHECK(S) FAILED\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
