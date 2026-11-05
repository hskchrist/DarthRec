#include "instance/instance.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "core/log.h"

namespace r8::instance {
namespace {

constexpr uint32_t k_magic = 0x38484352;  // "RCH8"
constexpr uint32_t k_layout_version = 1;

struct shared_instance {
    DWORD pid;
    DWORD reserved;
    uint64_t hwnd;
};

struct shared_claim {
    uint64_t key;
    int32_t instance;
    int32_t player;
};

struct shared_block {
    uint32_t magic;
    uint32_t version;
    volatile LONG lock;
    uint32_t pad;
    shared_instance instances[k_max_instances];
    shared_claim claims[k_max_claims];
};

HANDLE g_mapping = nullptr;
shared_block* g_block = nullptr;
unsigned g_index = 0;
std::atomic<HWND> g_window{nullptr};

std::mutex g_layout_mutex;
std::string g_layout = "none";
int g_monitor = 0;

class spin_lock {
public:
    spin_lock() {
        if (!g_block) return;
        int spins = 0;
        while (InterlockedCompareExchange(&g_block->lock, 1, 0) != 0) {
            if (++spins > 2000) break;  // nunca bloquear el juego para siempre
            Sleep(0);
        }
    }
    ~spin_lock() {
        if (g_block) InterlockedExchange(&g_block->lock, 0);
    }
};

bool process_alive(DWORD pid) {
    if (pid == 0) return false;
    HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!p) return false;
    bool alive = WaitForSingleObject(p, 0) == WAIT_TIMEOUT;
    CloseHandle(p);
    return alive;
}

void clean_dead() {
    for (int i = 0; i < k_max_instances; ++i) {
        if (g_block->instances[i].pid != 0 && !process_alive(g_block->instances[i].pid)) {
            R8_INFO("Instancia %d cerrada: se liberan sus mandos", i + 1);
            g_block->instances[i] = {};
            for (auto& c : g_block->claims) {
                if (c.key != 0 && c.instance == i) c = {};
            }
        }
    }
}

}  // namespace

bool initialize() {
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   sizeof(shared_block), L"Local\\Reach8");
    if (!g_mapping) {
        R8_WARN("Sin memoria compartida: esta ventana funcionará sola (instancia 1)");
        return false;
    }
    bool created = GetLastError() != ERROR_ALREADY_EXISTS;
    g_block = static_cast<shared_block*>(
        MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shared_block)));
    if (!g_block) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return false;
    }

    spin_lock lock;
    if (created || g_block->magic != k_magic || g_block->version != k_layout_version) {
        memset(g_block, 0, sizeof(shared_block));
        g_block->magic = k_magic;
        g_block->version = k_layout_version;
    }
    clean_dead();
    DWORD pid = GetCurrentProcessId();
    g_index = 0;
    for (int i = 0; i < k_max_instances; ++i) {
        if (g_block->instances[i].pid == 0) {
            g_block->instances[i].pid = pid;
            g_index = static_cast<unsigned>(i);
            R8_INFO("Esta ventana del juego es la instancia %d", i + 1);
            return true;
        }
    }
    R8_WARN("Ya hay %d instancias abiertas; esta se trata como instancia 1", k_max_instances);
    return true;
}

void shutdown() {
    if (!g_block) return;
    {
        spin_lock lock;
        if (g_block->instances[g_index].pid == GetCurrentProcessId()) {
            g_block->instances[g_index] = {};
        }
        for (auto& c : g_block->claims) {
            if (c.key != 0 && c.instance == static_cast<int32_t>(g_index)) c = {};
        }
    }
    UnmapViewOfFile(g_block);
    CloseHandle(g_mapping);
    g_block = nullptr;
    g_mapping = nullptr;
}

unsigned index() { return g_index; }

int alive_count() {
    if (!g_block) return 1;
    spin_lock lock;
    int n = 0;
    for (auto& i : g_block->instances) n += i.pid != 0 ? 1 : 0;
    return n;
}

bool claim(uint64_t key, int player) {
    if (key == 0) return false;
    if (!g_block) return true;
    spin_lock lock;
    clean_dead();
    shared_claim* free_slot = nullptr;
    for (auto& c : g_block->claims) {
        if (c.key == key) {
            if (c.instance == static_cast<int32_t>(g_index)) {
                c.player = player;
                return true;
            }
            return false;  // lo tiene otra ventana
        }
        if (c.key == 0 && free_slot == nullptr) free_slot = &c;
    }
    if (!free_slot) return false;
    *free_slot = {key, static_cast<int32_t>(g_index), player};
    return true;
}

void release(uint64_t key) {
    if (!g_block || key == 0) return;
    spin_lock lock;
    for (auto& c : g_block->claims) {
        if (c.key == key && c.instance == static_cast<int32_t>(g_index)) c = {};
    }
}

void release_mine() {
    if (!g_block) return;
    spin_lock lock;
    for (auto& c : g_block->claims) {
        if (c.key != 0 && c.instance == static_cast<int32_t>(g_index)) c = {};
    }
}

bool owner(uint64_t key, int* instance, int* player) {
    if (!g_block || key == 0) return false;
    spin_lock lock;
    for (auto& c : g_block->claims) {
        if (c.key == key) {
            if (instance) *instance = c.instance;
            if (player) *player = c.player;
            return true;
        }
    }
    return false;
}

void set_window(HWND hwnd) {
    g_window.store(hwnd);
    if (g_block) {
        spin_lock lock;
        g_block->instances[g_index].hwnd = reinterpret_cast<uint64_t>(hwnd);
    }
}

HWND window() { return g_window.load(); }

void request_layout(const std::string& layout, int monitor) {
    {
        std::lock_guard lock(g_layout_mutex);
        g_layout = layout;
        g_monitor = monitor;
    }
    if (HWND hwnd = g_window.load()) PostMessageW(hwnd, k_layout_message, 0, 0);
}

namespace {

struct monitor_search {
    int wanted;
    int current;
    RECT rect;
    bool found;
};

BOOL CALLBACK monitor_proc(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* search = reinterpret_cast<monitor_search*>(data);
    if (search->current++ == search->wanted) {
        MONITORINFO mi{sizeof(mi)};
        if (GetMonitorInfoW(monitor, &mi)) {
            search->rect = mi.rcMonitor;
            search->found = true;
        }
        return FALSE;
    }
    return TRUE;
}

}  // namespace

// Se llama desde el procedimiento de ventana (hilo de la ventana): así no se
// bloquea el hilo de render del juego con mensajes entre hilos.
void apply_layout(HWND hwnd) {
    std::string layout;
    int monitor = 0;
    {
        std::lock_guard lock(g_layout_mutex);
        layout = g_layout;
        monitor = g_monitor;
    }
    if (layout == "none" || layout.empty()) return;

    monitor_search search{monitor, 0, {}, false};
    EnumDisplayMonitors(nullptr, nullptr, monitor_proc, reinterpret_cast<LPARAM>(&search));
    if (!search.found) {
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        search.rect = mi.rcMonitor;
    }
    RECT r = search.rect;
    LONG w = r.right - r.left;
    LONG h = r.bottom - r.top;
    if (layout == "left") r.right = r.left + w / 2;
    else if (layout == "right") r.left = r.left + w / 2;
    else if (layout == "top") r.bottom = r.top + h / 2;
    else if (layout == "bottom") r.top = r.top + h / 2;
    // "full": monitor completo sin bordes

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    style |= WS_POPUP | WS_VISIBLE;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    R8_INFO("Ventana colocada: %s (monitor %d) %ldx%ld", layout.c_str(), monitor,
            r.right - r.left, r.bottom - r.top);
}

}  // namespace r8::instance
