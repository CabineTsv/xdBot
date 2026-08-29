#pragma once

// Thank you Peony for ts

#ifdef GEODE_IS_WINDOWS

#include <safetyhook.hpp>
#include <safetyhook/easy.hpp>

#include <string_view>
#include <vector>

namespace utils {

class MidhookManager {
public:
    static MidhookManager& get() {
        static MidhookManager instance;
        return instance;
    }

    void save(SafetyHookMid&& hook) {
        m_hooks.push_back(std::move(hook));
    }

private:
    MidhookManager() = default;
    std::vector<SafetyHookMid> m_hooks;
};

void midhook(uintptr_t address, std::string_view label, void (*callback)(safetyhook::Context&));

} // namespace utils

#endif
