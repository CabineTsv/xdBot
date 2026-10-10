#include "intentional_death.hpp"

#include "intentional_death_planner.hpp"

#include "../core/bot.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {

intentional_death::Planner g_planner;
intentional_death::Config g_config;

int clampValue(int64_t value, int low, int high) {
    return static_cast<int>(std::clamp<int64_t>(value, low, high));
}

int randomInt(int low, int high) {
    if (high <= low)
        return low;

    int span = high - low + 1;
    int value = geode::utils::random::generate(0, 1000000);
    return low + value % span;
}

float levelPercent(PlayLayer* pl) {
    float percent = 0.f;

    if (pl->m_level && pl->m_level->m_timestamp > 0 && Bot::getTPS() != 240.f) {
        auto timestamp = pl->m_level->m_timestamp;
        auto savedProgress = pl->m_gameState.m_currentProgress;

        double totalTime = timestamp / 240.0;
        double progress =
            totalTime == 0.0 ? 0.0 : (pl->m_gameState.m_levelTime / totalTime) * 100.0;

        pl->m_gameState.m_currentProgress = timestamp * progress * 2 / 100.f;
        percent = pl->getCurrentPercent();
        pl->m_gameState.m_currentProgress = savedProgress;
    } else {
        percent = pl->getCurrentPercent();
    }

    if (!std::isfinite(percent))
        return 0.f;

    return std::clamp(percent, 0.f, 100.f);
}

} // namespace

void IntentionalDeath::init() {
    auto* mod = Mod::get();

    if (!mod->setSavedValue("intentional_death_defaults", true)) {
        mod->setSavedValue("macro_intentional_death", false);
        mod->setSavedValue("intentional_death_min", 40);
        mod->setSavedValue("intentional_death_max", 60);
        mod->setSavedValue("intentional_death_frames", 4);
    }

    reload();
}

void IntentionalDeath::reload() {
    auto* mod = Mod::get();

    g_config.enabled = mod->getSavedValue<bool>("macro_intentional_death");
    g_config.minPercent = clampValue(mod->getSavedValue<int64_t>("intentional_death_min"), 0, 100);
    g_config.maxPercent = clampValue(mod->getSavedValue<int64_t>("intentional_death_max"), 0, 100);
    g_config.frames = clampValue(mod->getSavedValue<int64_t>("intentional_death_frames"),
                                 1,
                                 intentional_death::maxFrameShift);
}

bool IntentionalDeath::enabled() {
    return g_config.enabled;
}

void IntentionalDeath::reset() {
    g_planner.reset();
}

void IntentionalDeath::update(PlayLayer* pl) {
    if (!g_config.enabled || !pl || pl->m_isPlatformer) {
        g_planner.release();
        return;
    }

    auto before = g_planner.phase();
    g_planner.update(g_config, levelPercent(pl), randomInt);

    if (before == intentional_death::Planner::Phase::Idle &&
        g_planner.phase() == intentional_death::Planner::Phase::Armed) {
        log::debug("Intentional death armed at {:.2f}% (window {}% - {}%)",
                   g_planner.target(),
                   std::min(g_config.minPercent, g_config.maxPercent),
                   std::max(g_config.minPercent, g_config.maxPercent));
    }
}

bool IntentionalDeath::due(size_t index, uint64_t nominalFrame, int frame) {
    return g_planner.due(g_config, index, static_cast<int64_t>(nominalFrame), frame, randomInt);
}

void IntentionalDeath::executed(int frame) {
    g_planner.executed(frame);
}

bool IntentionalDeath::shifting() {
    return g_config.enabled && g_planner.isShifting();
}

bool IntentionalDeath::suppressFixes() {
    return g_config.enabled && g_planner.hasFailed();
}

$execute {
    IntentionalDeath::init();
};
