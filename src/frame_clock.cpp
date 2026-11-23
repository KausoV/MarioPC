#include "frame_clock.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mario {

NativeFrameClock::NativeFrameClock(std::uint64_t now) noexcept
    : nextDeadlineNS_(static_cast<double>(now)) {}

void NativeFrameClock::reset(std::uint64_t now) noexcept {
    nextDeadlineNS_ = static_cast<double>(now);
    if (rebaseCount_ != std::numeric_limits<std::uint64_t>::max()) ++rebaseCount_;
}

unsigned NativeFrameClock::due(std::uint64_t now) noexcept {
    const double timestamp = static_cast<double>(now);
    // Checking first avoids truncation toward zero for a negative fractional
    // interval, which would incorrectly advance the game before its deadline.
    if (timestamp < nextDeadlineNS_) return 0;

    const double ready = std::floor((timestamp - nextDeadlineNS_) / NativeFramePeriodNS) + 1;
    if (ready > CatchUpLimit) {
        const auto discarded = static_cast<std::uint64_t>(ready - CatchUpLimit);
        const auto room = std::numeric_limits<std::uint64_t>::max() - debtDrops_;
        debtDrops_ += std::min(discarded, room);
        // The next step is in the future; subsequent presentations cannot
        // repeatedly drain the old stall or replay a suspended window's time.
        nextDeadlineNS_ = timestamp + NativeFramePeriodNS;
        return CatchUpLimit;
    }

    const auto steps = static_cast<unsigned>(ready);
    nextDeadlineNS_ += NativeFramePeriodNS * steps;
    return steps;
}

} // namespace mario
