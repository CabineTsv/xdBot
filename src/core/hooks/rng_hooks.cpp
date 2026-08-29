// we love Silicate ❤️
#ifdef GEODE_IS_WINDOWS

#include "../bot.hpp"
#include "../../utils/midhook.hpp"

#include <Geode/Geode.hpp>

namespace {

void advance(uint64_t& state) {
    state = (state * 214013 + 2531011) >> 16 & 0x7FFF;
}

void shakeRandomOverride(safetyhook::Context& ctx) {
    auto& state = Bot::get().shakeRandomState;
    advance(state);
    ctx.rax = static_cast<uintptr_t>(state);
}

void teleportRandomOverride(safetyhook::Context& ctx) {
    auto& state = Bot::get().teleportRandomState;
    advance(state);
    ctx.rax = static_cast<uintptr_t>(state);
}

} // namespace

$execute {
    ::utils::midhook(geode::base::get() + 0x23E173, "shakeRandomOverride", shakeRandomOverride);
    ::utils::midhook(geode::base::get() + 0x23E1A1, "shakeRandomOverride", shakeRandomOverride);
    ::utils::midhook(geode::base::get() + 0x23E1CB, "shakeRandomOverride", shakeRandomOverride);
    ::utils::midhook(geode::base::get() + 0x23E1E9, "shakeRandomOverride", shakeRandomOverride);
    ::utils::midhook(geode::base::get() + 0x20FED3, "teleportRandomOverride", teleportRandomOverride);
}

#endif
