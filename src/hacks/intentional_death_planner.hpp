#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace intentional_death {

constexpr int maxFrameShift = 60;

struct Config {
    bool enabled = false;
    int minPercent = 40;
    int maxPercent = 60;
    int frames = 4;
};

using RandFn = int (*)(int low, int high);

class Planner {
  public:
    enum class Phase : uint8_t { Idle, Armed, Failing, Done };

    void reset() {
        phase_ = Phase::Idle;
        rolled_ = false;
        failed_ = false;
        shifted_ = false;
        pending_ = false;
        pendingIndex_ = 0;
        pendingDue_ = 0;
        lastExecuted_ = -1;
        target_ = 0.f;
    }

    void release() {
        if (phase_ != Phase::Idle || rolled_ || pending_ || shifted_ || failed_)
            reset();
    }

    void update(Config const& cfg, float percent, RandFn rand) {
        if (!cfg.enabled)
            return release();

        float low = static_cast<float>(std::clamp(std::min(cfg.minPercent, cfg.maxPercent), 0, 100));
        float high = static_cast<float>(std::clamp(std::max(cfg.minPercent, cfg.maxPercent), 0, 100));

        if (!rolled_) {
            rolled_ = true;

            float start = std::max(low, percent);
            if (start > high) {
                phase_ = Phase::Done;
                return;
            }

            float roll = static_cast<float>(rand(0, 10000)) / 10000.f;
            target_ = start + (high - start) * roll;
            phase_ = Phase::Armed;
        }

        if (phase_ == Phase::Armed && percent >= target_) {
            phase_ = Phase::Failing;
            failed_ = true;
        }

        if (phase_ == Phase::Failing && percent > high)
            phase_ = Phase::Done;
    }

    bool due(Config const& cfg, size_t index, int64_t nominal, int64_t frame, RandFn rand) {
        if (!cfg.enabled)
            return frame >= nominal;

        if (pending_) {
            if (pendingIndex_ == index)
                return frame >= pendingDue_;
            pending_ = false;
        }

        if (phase_ != Phase::Failing)
            return frame >= nominal;

        int maxShift = std::clamp(cfg.frames, 1, maxFrameShift);
        if (frame < nominal - maxShift)
            return false;

        decide(maxShift, index, nominal, frame, rand);
        return frame >= pendingDue_;
    }

    void executed(int64_t frame) {
        lastExecuted_ = frame;
        pending_ = false;
    }

    Phase phase() const {
        return phase_;
    }

    float target() const {
        return target_;
    }

    bool isShifting() const {
        return shifted_ || phase_ == Phase::Failing;
    }

    bool hasFailed() const {
        return failed_;
    }

  private:
    void decide(int maxShift, size_t index, int64_t nominal, int64_t frame, RandFn rand) {
        int minShift = std::max(1, (maxShift + 1) / 2);
        int64_t magnitude = rand(minShift, maxShift);
        bool early = rand(0, 1) == 0;

        int64_t earliest = std::max(frame, lastExecuted_);

        if (early) {
            int64_t room = std::max<int64_t>(nominal - earliest, 0);
            int64_t usable = std::min(magnitude, room);
            if (usable < minShift)
                early = false;
            else
                magnitude = usable;
        }

        int64_t target = early ? nominal - magnitude : nominal + magnitude;

        pending_ = true;
        pendingIndex_ = index;
        pendingDue_ = std::max(target, earliest);
        shifted_ = true;
    }

    Phase phase_ = Phase::Idle;
    bool rolled_ = false;
    bool failed_ = false;
    bool shifted_ = false;
    bool pending_ = false;
    size_t pendingIndex_ = 0;
    int64_t pendingDue_ = 0;
    int64_t lastExecuted_ = -1;
    float target_ = 0.f;
};

} // namespace intentional_death
