#pragma once

class InputBuffer {
  public:
    static void init();

    static void reload();

    static bool enabled();

    static bool active();

    static void reset();
};
