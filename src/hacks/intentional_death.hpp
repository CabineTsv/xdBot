#pragma once

#include <cstddef>
#include <cstdint>

class PlayLayer;

class IntentionalDeath {
  public:
    static void init();

    static void reload();

    static bool enabled();

    static void reset();

    static void update(PlayLayer* pl);

    static bool due(size_t index, uint64_t nominalFrame, int frame);

    static void executed(int frame);

    static bool shifting();

    static bool suppressFixes();
};
