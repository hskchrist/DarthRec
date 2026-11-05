#include "core/hooks.h"

#include <windows.h>

#include <MinHook.h>

#include <mutex>
#include <vector>

#include "core/log.h"
#include "core/memory.h"

namespace r8::hooks {
namespace {

struct slot_record {
    void** slot;
    void* original;
};

std::mutex g_mutex;
std::vector<void*> g_function_hooks;
std::vector<slot_record> g_slots;
bool g_initialized = false;

}  // namespace

bool initialize() {
    std::lock_guard lock(g_mutex);
    if (g_initialized) return true;
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        R8_ERROR("MinHook no se pudo iniciar: %s", MH_StatusToString(status));
        return false;
    }
    g_initialized = true;
    return true;
}

bool create(void* target, void* detour, void** original, const char* name) {
    std::lock_guard lock(g_mutex);
    if (!g_initialized || target == nullptr) return false;
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK) {
        R8_ERROR("Hook '%s' no creado: %s", name, MH_StatusToString(status));
        return false;
    }
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        R8_ERROR("Hook '%s' no activado: %s", name, MH_StatusToString(status));
        MH_RemoveHook(target);
        return false;
    }
    g_function_hooks.push_back(target);
    R8_INFO("Hook '%s' activo en %p", name, target);
    return true;
}

bool vtable_slot(void** vtable, int index, void* detour, void** original, const char* name) {
    std::lock_guard lock(g_mutex);
    void** slot = vtable + index;
    if (!memory::is_readable(reinterpret_cast<uintptr_t>(slot), sizeof(void*))) return false;
    void* previous = *slot;
    if (!memory::is_executable(reinterpret_cast<uintptr_t>(previous))) {
        R8_ERROR("Entrada %d de la tabla (%s) no apunta a código: %p", index, name, previous);
        return false;
    }
    *original = previous;
    if (!memory::write_pointer(slot, detour)) {
        R8_ERROR("No se pudo escribir la entrada %d (%s)", index, name);
        return false;
    }
    g_slots.push_back({slot, previous});
    R8_INFO("Tabla MCC: entrada %d (%s) enganchada", index, name);
    return true;
}

void remove_all() {
    std::lock_guard lock(g_mutex);
    for (auto it = g_slots.rbegin(); it != g_slots.rend(); ++it) {
        memory::write_pointer(it->slot, it->original);
    }
    g_slots.clear();
    for (void* target : g_function_hooks) {
        MH_DisableHook(target);
        MH_RemoveHook(target);
    }
    g_function_hooks.clear();
}

void shutdown() {
    remove_all();
    std::lock_guard lock(g_mutex);
    if (g_initialized) {
        MH_Uninitialize();
        g_initialized = false;
    }
}

}  // namespace r8::hooks
