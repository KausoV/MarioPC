#include "frame_clock.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
unsigned checks = 0, failures = 0;
void check(const std::string& name, bool passed) {
    ++checks;
    if (!passed) ++failures;
    std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
}
std::uint64_t ceiling(double ns) { return static_cast<std::uint64_t>(std::ceil(ns)); }

void boundaries() {
    constexpr std::uint64_t start = 12'345'678'901ULL;
    mario::NativeFrameClock clock(start);
    check("constructor has no explicit rebases", clock.rebaseCount() == 0);
    check("before the initial deadline has no game step", clock.due(start - 1) == 0);
    check("the initial deadline has exactly one game step", clock.due(start) == 1);
    check("repeated presentation at one timestamp does not repeat a game step", clock.due(start) == 0);
    const auto next = ceiling(clock.nextDeadlineNS());
    check("one nanosecond before the next deadline does not tick", clock.due(next - 1) == 0);
    check("the first representable timestamp at the next deadline ticks", clock.due(next) == 1);
    check("an intermediate high-refresh presentation does not tick", clock.due(next + 1'000'000) == 0);
    const auto third = ceiling(clock.nextDeadlineNS());
    check("two consecutive deadlines preserve the fractional native period",
          std::abs(clock.nextDeadlineNS() - (start + 2 * mario::NativeFramePeriodNS)) < 0.01);
    check("the third exact deadline ticks once", clock.due(third) == 1);
    check("ordinary deadlines do not discard game time", clock.debtDrops() == 0);
}

void monitorRates() {
    // Simulate presentation timestamps, not a rewritten gameplay algorithm.
    // The independent expected count is original native ticks in 1000 seconds.
    constexpr std::uint64_t second = 1'000'000'000ULL;
    constexpr unsigned durationSeconds = 1000;
    const auto expected = static_cast<std::uint64_t>(std::floor(durationSeconds * mario::NativeFrameRate)) + 1;
    for (unsigned refresh : {24u, 50u, 60u, 75u, 90u, 120u, 144u, 165u, 240u}) {
        mario::NativeFrameClock clock;
        std::uint64_t ticks = 0, duplicateFrames = 0;
        unsigned maxDue = 0;
        const auto presentations = std::uint64_t(refresh) * durationSeconds;
        for (std::uint64_t frame = 0; frame <= presentations; ++frame) {
            const auto timestamp = frame * second / refresh;
            const auto due = clock.due(timestamp);
            ticks += due;
            duplicateFrames += due == 0;
            maxDue = std::max(maxDue, due);
        }
        const std::string prefix = std::to_string(refresh) + " Hz: ";
        check(prefix + "1000 seconds preserve the original native tick count", ticks == expected);
        check(prefix + "normal monitor cadence drops no gameplay steps", clock.debtDrops() == 0);
        check(prefix + "next native deadline stays beyond the last presentation",
              clock.nextDeadlineNS() > durationSeconds * double(second));
        check(prefix + "native phase stays within 1 ms over 1000 seconds",
              std::abs(clock.nextDeadlineNS() - ticks * mario::NativeFramePeriodNS) < 1'000'000.0);
        if (refresh > mario::NativeFrameRate) {
            check(prefix + "extra monitor frames duplicate the current game image", duplicateFrames > 0);
            check(prefix + "fast monitors never accelerate gameplay", maxDue == 1);
        } else {
            check(prefix + "slower monitors catch up the original game rate", maxDue >= 2);
        }
    }
}

void stallsAndResumes() {
    mario::NativeFrameClock clock;
    check("stall fixture starts with a native frame", clock.due(0) == 1);
    const auto shortStall = ceiling(3 * mario::NativeFramePeriodNS);
    check("a short stall catches up three real native steps", clock.due(shortStall) == 3);
    check("short catch-up retains all native time", clock.debtDrops() == 0);
    check("short catch-up leaves no duplicate step at the same timestamp", clock.due(shortStall) == 0);

    clock.reset(0);
    check("explicit reset is counted once", clock.rebaseCount() == 1);
    check("reset makes the first step immediately available", clock.due(0) == 1);
    constexpr std::uint64_t longStall = 2'000'000'000ULL;
    const auto overdue = static_cast<std::uint64_t>(std::floor(longStall / mario::NativeFramePeriodNS));
    check("a two-second stall cannot create an unbounded catch-up burst",
          clock.due(longStall) == mario::NativeFrameClock::CatchUpLimit);
    check("discarded native ticks are counted independently of rendered frames",
          clock.debtDrops() == overdue - mario::NativeFrameClock::CatchUpLimit);
    check("a long stall rebases its next deadline to a future native interval",
          std::abs(clock.nextDeadlineNS() - (longStall + mario::NativeFramePeriodNS)) < 0.01);
    check("the same stalled timestamp cannot drain another burst", clock.due(longStall) == 0);
    check("a sub-period monitor frame after a stall still does not tick", clock.due(longStall + 1'000'000) == 0);
    check("the first post-stall deadline resumes at one native step",
          clock.due(ceiling(clock.nextDeadlineNS())) == 1);

    const auto droppedBeforeResume = clock.debtDrops();
    constexpr std::uint64_t resumedAt = 600'000'000'000ULL;
    clock.reset(resumedAt);
    check("paused resume has an immediate single step instead of replaying minutes", clock.due(resumedAt) == 1);
    check("paused time is not treated as discarded gameplay debt", clock.debtDrops() == droppedBeforeResume);
    check("paused resume consumes the single initial step once", clock.due(resumedAt) == 0);
    clock.reset(resumedAt + 10'000'000'000ULL);
    check("display-mode transition reset cannot fast-forward the game", clock.due(resumedAt + 10'000'000'000ULL) == 1);
    check("resume and display resets retain lifetime telemetry", clock.rebaseCount() == 3 && clock.debtDrops() == droppedBeforeResume);

    // A user-visible F2/game reset can start a fresh local epoch safely.
    clock.reset(500);
    check("a fresh game reset discards the old clock phase", clock.due(499) == 0 && clock.due(500) == 1);
    check("game reset does not erase prior stall telemetry", clock.debtDrops() == droppedBeforeResume);
}

void lateBoundaries() {
    mario::NativeFrameClock clock;
    clock.due(0);
    const auto eighthReady = ceiling(8 * mario::NativeFramePeriodNS);
    check("eight ready steps are all retained", clock.due(eighthReady) == 8 && clock.debtDrops() == 0);
    check("eight-step catch-up preserves phase", std::abs(clock.nextDeadlineNS() - 9 * mario::NativeFramePeriodNS) < 0.01);
    clock.reset(0);
    clock.due(0);
    const auto ninthReady = ceiling(9 * mario::NativeFramePeriodNS);
    check("nine ready steps are bounded to eight", clock.due(ninthReady) == 8);
    check("only the one excess overdue step is discarded", clock.debtDrops() == 1);
    check("an over-limit burst cannot repeat on the next high-refresh image", clock.due(ninthReady + 1) == 0);
}
}

int main() {
    boundaries(); monitorRates(); stallsAndResumes(); lateBoundaries();
    std::cout << "RESULT " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
