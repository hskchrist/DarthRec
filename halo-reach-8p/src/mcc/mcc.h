#pragma once

#include <cstdint>
#include <string>

#include "mcc/mcc_types.h"

namespace r8::mcc {

enum class store { steam, winstore, unknown };

// Perfil de build: decide qué parches conocidos de haloreach.dll se pueden usar.
enum class build_profile {
    unknown,
    v1_3385,       // MCC 1.3385.0.0 (offsets de AlphaRing master)
    march_2025,    // build de la actualización de marzo de 2025 (offsets de libmcc / AlphaRing dev)
};

struct status {
    bool resolved = false;
    bool hooks_ok = false;
    store which = store::unknown;
    std::string exe_version;
    uintptr_t vtable_rva = 0;
    uintptr_t game_manager_rva = 0;
    build_profile profile = build_profile::unknown;
    std::string method;  // "firma" o "offset conocido"
};

bool initialize();
void shutdown();
const status& get_status();
const char* profile_name(build_profile p);

// XUID del usuario real (jugador 1). 0 si todavía no se conoce.
XUID primary_xuid();

}  // namespace r8::mcc
