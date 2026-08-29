// used Silicate as a reference
#ifdef GEODE_IS_WINDOWS

#include "midhook.hpp"

#include <Geode/Geode.hpp>

namespace utils {

void midhook(uintptr_t address, std::string_view label, void (*callback)(safetyhook::Context&)) {
    MidhookManager::get().save(safetyhook::create_mid(address, callback));
    geode::log::debug("[xdBot] installed midhook '{}' at 0x{:x}", label, address);
}

} // namespace utils

#endif
