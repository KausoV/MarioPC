#pragma once

#include <cstdint>

namespace mario {

// The original NTSC game rate. Presentation refresh must not alter gameplay.
inline constexpr double NativeFrameRate = 60.0988138974405;
inline constexpr double NativeFramePeriodNS = 1.0e9 / NativeFrameRate;

class NativeFrameClock {
public:
    static constexpr unsigned CatchUpLimit = 8;

    // The first native step is ready immediately; times are monotonic nanoseconds.
    explicit NativeFrameClock(std::uint64_t now = 0) noexcept;
    // Use on resume/reset or a blocking display-mode transition. Never catch up
    // the time spent paused or switching modes. Lifetime telemetry is retained.
    void reset(std::uint64_t now) noexcept;
    // Consume ready native steps once. A future deadline produces zero steps.
    // Long stalls consume at most CatchUpLimit and discard excess old debt.
    unsigned due(std::uint64_t now) noexcept;

    double nextDeadlineNS() const noexcept { return nextDeadlineNS_; }
    std::uint64_t debtDrops() const noexcept { return debtDrops_; }
    std::uint64_t rebaseCount() const noexcept { return rebaseCount_; }

private:
    double nextDeadlineNS_ = 0;
    std::uint64_t debtDrops_ = 0;
    std::uint64_t rebaseCount_ = 0;
};

} // namespace mario
