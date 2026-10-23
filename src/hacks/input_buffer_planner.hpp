#pragma once

#include <algorithm>
#include <cstdint>

namespace input_buffer {

constexpr int tickOptions[] = {2, 4, 8, 16};
constexpr int defaultTicks = 4;
constexpr int maxTicks = 16;
constexpr int ringLagTicks = 1;

inline int normalizeTicks(int ticks) {
    for (int option : tickOptions) {
        if (ticks == option)
            return ticks;
    }

    return defaultTicks;
}

struct Config {
    bool enabled = false;
    int ticks = defaultTicks;
};

struct Observation {
    bool needsOpportunity = false;
    bool grounded = false;
    bool touchingRing = false;
    bool consumed = false;
    bool blocked = false;

    bool opportunity() const {
        return grounded || touchingRing;
    }
};

struct Output {
    bool press = false;
    bool release = false;
    int delayedTicks = 0;
};

class Buffer {
  public:
    enum class Phase : uint8_t { Idle, Armed, Releasing };

    void reset() {
        phase_ = Phase::Idle;
        released_ = false;
        pressTick_ = 0;
        releaseTick_ = 0;
        deliveredTick_ = 0;
        releaseDue_ = 0;
    }

    void onPress(int tick, Observation const& obs, Config const& cfg) {
        reset();

        if (!cfg.enabled)
            return;

        if (!obs.needsOpportunity || obs.blocked || obs.opportunity())
            return;

        phase_ = Phase::Armed;
        pressTick_ = tick;
        releaseTick_ = tick;
    }

    void onRelease(int tick) {
        if (phase_ != Phase::Armed || released_)
            return;

        released_ = true;
        releaseTick_ = std::max(tick, pressTick_);
    }

    Output update(int tick, Observation const& obs, Config const& cfg) {
        Output out;

        if (!cfg.enabled) {
            reset();
            return out;
        }

        if (phase_ == Phase::Releasing) {
            if (tick < deliveredTick_) {
                reset();
                return out;
            }

            if (tick >= releaseDue_) {
                out.release = true;
                reset();
            }

            return out;
        }

        if (phase_ != Phase::Armed)
            return out;

        int window = std::clamp(cfg.ticks, 0, maxTicks);
        int elapsed = tick - pressTick_;

        if (elapsed < 0 || elapsed > window + ringLagTicks || !obs.needsOpportunity) {
            reset();
            return out;
        }

        if (obs.consumed) {
            reset();
            return out;
        }

        if (obs.blocked || !obs.opportunity())
            return out;

        int lag = obs.grounded || !obs.touchingRing ? 0 : ringLagTicks;
        if (elapsed - lag > window || !released_) {
            reset();
            return out;
        }

        elapsed -= lag;

        int hold = std::max(releaseTick_ - pressTick_, 0);

        out.press = true;
        out.delayedTicks = elapsed;

        if (hold == 0) {
            out.release = true;
            reset();
            return out;
        }

        phase_ = Phase::Releasing;
        deliveredTick_ = tick;
        releaseDue_ = tick + hold;
        return out;
    }

    Phase phase() const {
        return phase_;
    }

    bool idle() const {
        return phase_ == Phase::Idle;
    }

  private:
    Phase phase_ = Phase::Idle;
    bool released_ = false;
    int pressTick_ = 0;
    int releaseTick_ = 0;
    int deliveredTick_ = 0;
    int releaseDue_ = 0;
};

} // namespace input_buffer
