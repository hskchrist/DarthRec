#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mcc/mcc.h"

namespace r8::reach {

struct patch_status {
    std::string name;
    uintptr_t rva = 0;
    std::string state;     // "aplicado", "omitido: ...", "pendiente"
    std::string original;  // bytes originales (hex)
};

// Vigila la carga de haloreach.dll (cada vez que se inicia una partida de Reach)
// y aplica los parches del perfil de build detectado.
bool initialize(mcc::build_profile profile);
void shutdown();

bool loaded();
std::string dll_version();
std::vector<patch_status> status();

}  // namespace r8::reach
