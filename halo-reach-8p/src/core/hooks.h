#pragma once

#include <string>

namespace r8::hooks {

bool initialize();
void shutdown();

// Hook de función (MinHook). Devuelve false si no se pudo crear o activar.
bool create(void* target, void* detour, void** original, const char* name);

// Sustituye una entrada de una tabla virtual. Se restaura en shutdown().
bool vtable_slot(void** vtable, int index, void* detour, void** original, const char* name);

void remove_all();

}  // namespace r8::hooks
