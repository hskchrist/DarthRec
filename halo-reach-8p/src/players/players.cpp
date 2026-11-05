#include "players/players.h"

#include <atomic>
#include <cwchar>
#include <mutex>

#include "core/config.h"
#include "core/log.h"
#include "core/paths.h"

namespace r8::players {
namespace {

std::atomic<int> g_count{1};
std::atomic<uint64_t> g_seed{0};
unsigned g_instance = 0;

std::mutex g_mutex;
std::wstring g_names[config::k_max_local_players];

}  // namespace

void initialize(unsigned instance) {
    g_instance = instance;
    reload();
}

void reload() {
    auto s = config::get();
    g_seed.store(s.xuid_seed & ~0xFFull);
    g_count.store(s.local_players);
    std::lock_guard lock(g_mutex);
    for (int i = 0; i < config::k_max_local_players; ++i) {
        g_names[i] = paths::widen(s.players[i].name);
        if (g_names[i].empty()) g_names[i] = L"Jugador " + std::to_wstring(global_number(i));
    }
}

int local_count() { return g_count.load(std::memory_order_relaxed); }

void set_local_count(int count) {
    if (count < 1) count = 1;
    if (count > config::k_max_local_players) count = config::k_max_local_players;
    g_count.store(count);
    R8_INFO("Jugadores locales en esta instancia: %d", count);
}

bool is_fake(mcc::XUID xuid) {
    uint64_t seed = g_seed.load(std::memory_order_relaxed);
    return seed != 0 && (xuid & ~0xFFull) == seed;
}

mcc::XUID xuid_for(int player) {
    return g_seed.load() | (static_cast<uint64_t>(g_instance & 0xF) << 4) |
           static_cast<uint64_t>(player & 0xF);
}

std::wstring name_for(int player) {
    std::lock_guard lock(g_mutex);
    if (player < 0 || player >= config::k_max_local_players) return L"?";
    return g_names[player];
}

bool fill_user(int player, mcc::XUID* xuid, wchar_t* name, uint32_t size_bytes) {
    if (player <= 0 || player >= local_count()) return false;
    if (xuid != nullptr) *xuid = xuid_for(player);
    if (name != nullptr && size_bytes >= sizeof(wchar_t)) {
        std::wstring n = name_for(player);
        size_t capacity = size_bytes / sizeof(wchar_t);
        size_t len = n.size() < capacity - 1 ? n.size() : capacity - 1;
        wmemcpy(name, n.c_str(), len);
        name[len] = L'\0';
    }
    return true;
}

int global_number(int player) { return static_cast<int>(g_instance) * 4 + player + 1; }

}  // namespace r8::players
