#include "netprobe/netprobe.h"

#include <atomic>

#include "core/hooks.h"
#include "core/log.h"
#include "mcc/mcc_types.h"

namespace r8::netprobe {
namespace {

// Paso genérico: reenvía los cuatro registros y el primer argumento de pila tal
// cual, de modo que funciona aunque la firma exacta no esté confirmada.
using fn_generic = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

fn_generic g_original[4] = {};
std::atomic<uint64_t> g_calls[4];
std::atomic<uint64_t> g_bytes[4];
bool g_installed = false;

// sendto(this, id, buffer, size, port) -> el tamaño va en el 4.º argumento.
// recvfrom(this, buffer, size, id*, port*) -> el tamaño máximo va en el 3.º.
template <int I>
uintptr_t probe(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    g_calls[I].fetch_add(1, std::memory_order_relaxed);
    uintptr_t result = g_original[I](a, b, c, d, e);
    uint64_t size = 0;
    if constexpr (I == 0 || I == 1) {
        size = static_cast<uint32_t>(d);
    } else if constexpr (I == 2) {
        size = static_cast<uint32_t>(result);  // bytes recibidos (si la firma es la esperada)
    }
    (void)a; (void)b; (void)c; (void)e;
    if (size < (1u << 20)) g_bytes[I].fetch_add(size, std::memory_order_relaxed);
    return result;
}

constexpr int k_slots[4] = {mcc::slot::network_sendto_unreliable, mcc::slot::network_sendto_reliable,
                            mcc::slot::network_recvfrom, mcc::slot::network_send};
constexpr const char* k_names[4] = {"envío no fiable", "envío fiable", "recepción", "envío (otro)"};

}  // namespace

void install(void** vtable) {
    void* detours[4] = {reinterpret_cast<void*>(&probe<0>), reinterpret_cast<void*>(&probe<1>),
                        reinterpret_cast<void*>(&probe<2>), reinterpret_cast<void*>(&probe<3>)};
    int ok = 0;
    for (int i = 0; i < 4; ++i) {
        if (hooks::vtable_slot(vtable, k_slots[i], detours[i],
                               reinterpret_cast<void**>(&g_original[i]), k_names[i]))
            ++ok;
    }
    g_installed = ok > 0;
    R8_INFO("netprobe: %d/4 funciones de red vigiladas (solo recuento)", ok);
}

bool installed() { return g_installed; }

counters snapshot() {
    counters c{};
    for (int i = 0; i < 4; ++i) {
        c.calls[i] = g_calls[i].load(std::memory_order_relaxed);
        c.bytes[i] = g_bytes[i].load(std::memory_order_relaxed);
    }
    return c;
}

const char* slot_name(int i) { return i >= 0 && i < 4 ? k_names[i] : "?"; }

}  // namespace r8::netprobe
