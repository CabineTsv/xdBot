#include "input_buffer.hpp"

#include "input_buffer_planner.hpp"

#include "../core/bot.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/RingObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <array>
#include <cmath>
#include <string>

using namespace geode::prelude;

namespace {

input_buffer::Config g_config;
std::array<input_buffer::Buffer, 2> g_buffers;
std::array<input_buffer::Observation, 2> g_observations;
constexpr size_t maxTrackedRings = 4;

struct TouchedRings {
    std::array<int, maxTrackedRings> ids = {};
    size_t count = 0;

    void add(int id) {
        for (size_t i = 0; i < count; i++) {
            if (ids[i] == id)
                return;
        }

        if (count < maxTrackedRings)
            ids[count++] = id;
    }

    void clear() {
        count = 0;
    }
};

std::array<TouchedRings, 2> g_touchedRings;
int g_lastTick = 0;
bool g_hasTick = false;
bool g_injecting = false;

int parseTicks(std::string const& value) {
    int ticks = 0;

    for (char c : value) {
        if (c < '0' || c > '9' || ticks > 1000)
            break;

        ticks = ticks * 10 + (c - '0');
    }

    return input_buffer::normalizeTicks(ticks);
}

int tickFor(PlayLayer* pl) {
    double ticks = pl->m_gameState.m_levelTime * static_cast<double>(Bot::getTPS());
    return static_cast<int>(std::floor(ticks + 1e-3));
}

input_buffer::Observation observe(PlayerObject* player, TouchedRings const& rings) {
    input_buffer::Observation obs;

    if (!player)
        return obs;

    bool pending = false;
    bool activated = false;

    for (size_t i = 0; i < rings.count; i++) {
        if (player->m_ringRelatedSet.find(rings.ids[i]) != player->m_ringRelatedSet.end())
            activated = true;
        else
            pending = true;
    }

    obs.needsOpportunity =
        !(player->m_isShip || player->m_isBird || player->m_isDart || player->m_isSwing);
    obs.grounded = player->m_isOnGround;
    obs.touchingRing = pending && !activated;
    obs.consumed = activated;
    obs.blocked = player->m_isDead || player->m_isDashing;

    return obs;
}

void resetAll() {
    for (auto& buffer : g_buffers)
        buffer.reset();

    g_observations = {};

    for (auto& rings : g_touchedRings)
        rings.clear();

    g_hasTick = false;
}

} // namespace

void InputBuffer::init() {
    auto* mod = Mod::get();

    geode::listenForSettingChanges<bool>("input_buffer", +[](bool value) {
        g_config.enabled = value;
        resetAll();
    }, mod);

    geode::listenForSettingChanges<std::string>("input_buffer_ticks", +[](std::string value) {
        g_config.ticks = parseTicks(value);
        resetAll();
    }, mod);

    reload();
}

void InputBuffer::reload() {
    auto* mod = Mod::get();

    g_config.enabled = mod->getSettingValue<bool>("input_buffer");
    g_config.ticks = parseTicks(mod->getSettingValue<std::string>("input_buffer_ticks"));

    resetAll();
}

bool InputBuffer::enabled() {
    return g_config.enabled;
}

bool InputBuffer::active() {
    if (!g_config.enabled)
        return false;

    auto& bot = Bot::get();

    return bot.state != state::playing && !bot.autoclicker && !bot.frameStepper &&
           !bot.creatingTrajectory;
}

void InputBuffer::reset() {
    resetAll();
}

class $modify(InputBufferGJBaseGameLayer, GJBaseGameLayer) {

    void runInputBuffer(PlayLayer* pl, int tick) {
        for (int index = 0; index < 2; index++) {
            PlayerObject* player = index == 0 ? pl->m_player1 : pl->m_player2;

            auto obs = observe(player, g_touchedRings[index]);
            g_touchedRings[index].clear();
            g_observations[index] = obs;

            auto out = g_buffers[index].update(tick, obs, g_config);
            if (!out.press && !out.release)
                continue;

            if (out.press) {
                log::debug("Input buffer: applied tap for player {} after {} tick(s) (window {})",
                           index + 1,
                           out.delayedTicks,
                           g_config.ticks);
            }

            g_injecting = true;

            if (out.press)
                GJBaseGameLayer::handleButton(true, 1, index == 0);

            if (out.release)
                GJBaseGameLayer::handleButton(false, 1, index == 0);

            g_injecting = false;
        }
    }

    void processQueuedButtons(float dt, bool clearInputQueue) {
        auto* pl = PlayLayer::get();

        if (!pl || !InputBuffer::active() || pl->m_isPaused || pl->m_levelEndAnimationStarted ||
            !pl->m_player1 || pl->m_player1->m_isDead) {
            if (g_hasTick)
                resetAll();

            return GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
        }

        int tick = tickFor(pl);

        if (g_hasTick && tick < g_lastTick)
            resetAll();

        if (!g_hasTick || tick != g_lastTick) {
            g_hasTick = true;
            g_lastTick = tick;
            runInputBuffer(pl, tick);
        }

        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (g_injecting || button != 1 || !g_hasTick || !InputBuffer::active())
            return GJBaseGameLayer::handleButton(down, button, isPlayer1);

        int index = isPlayer1 ? 0 : 1;
        auto& buffer = g_buffers[index];

        if (down) {
            buffer.onPress(g_lastTick, g_observations[index], g_config);

            if (buffer.phase() == input_buffer::Buffer::Phase::Armed) {
                log::debug("Input buffer: tap armed for player {} at tick {}", index + 1,
                           g_lastTick);
            }
        } else {
            buffer.onRelease(g_lastTick);
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        auto* pl = PlayLayer::get();

        if (pl && player && ring && g_hasTick && InputBuffer::active() &&
            (player == pl->m_player1 || player == pl->m_player2) &&
            player->m_ringRelatedSet.find(ring->m_uniqueID) == player->m_ringRelatedSet.end()) {
            g_touchedRings[player == pl->m_player1 ? 0 : 1].add(ring->m_uniqueID);
        }

        GJBaseGameLayer::playerTouchedRing(player, ring);
    }
};

class $modify(InputBufferPlayLayer, PlayLayer) {

    void resetLevel() {
        InputBuffer::reset();
        PlayLayer::resetLevel();
        InputBuffer::reset();
    }

    void pauseGame(bool b1) {
        InputBuffer::reset();
        PlayLayer::pauseGame(b1);
    }

    void onQuit() {
        InputBuffer::reset();
        PlayLayer::onQuit();
    }
};

$execute {
    InputBuffer::init();
}
