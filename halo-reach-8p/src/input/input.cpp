#include "input/input.h"

#include <windows.h>
#include <xinput.h>

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>

#include "core/config.h"
#include "core/log.h"
#include "instance/instance.h"
#include "players/players.h"

namespace r8::input {
namespace {

constexpr int k_players = config::k_max_local_players;

// Bits de XINPUT_GAMEPAD.wButtons
constexpr uint32_t k_dpad_up = 0x0001, k_dpad_down = 0x0002, k_dpad_left = 0x0004,
                   k_dpad_right = 0x0008, k_start = 0x0010, k_back = 0x0020,
                   k_lthumb = 0x0040, k_rthumb = 0x0080, k_lshoulder = 0x0100,
                   k_rshoulder = 0x0200, k_a = 0x1000, k_b = 0x2000, k_x = 0x4000,
                   k_y = 0x8000;

struct device {
    uint64_t key = 0;
    SDL_JoystickID id = 0;
    SDL_Gamepad* pad = nullptr;  // modo SDL
    int xinput_user = -1;        // modo XInput
    std::string name;
    std::string kind;
    mcc::gamepad_state state{};
    uint32_t previous_buttons = 0;
};

struct player_slot {
    bool km = false;
    uint64_t key = 0;
    uint16_t rumble_low = 0, rumble_high = 0;
    bool rumble_dirty = false;
};

std::mutex g_mutex;
std::vector<device> g_devices;
player_slot g_slots[k_players];
std::atomic<int> g_assigning{-1};
std::atomic<bool> g_running{false};
std::atomic<bool> g_config_dirty{true};
std::atomic<bool> g_menu_combo{false};
std::thread g_thread;
bool g_sdl = false;
std::string g_backend = "ninguno";

uint64_t fnv1a(const void* data, size_t size, uint64_t h = 1469598103934665603ull) {
    auto p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

int16_t invert(int16_t v) { return v == -32768 ? 32767 : static_cast<int16_t>(-v); }

uint8_t trigger(int16_t v) { return v <= 0 ? 0 : static_cast<uint8_t>((v * 255) / 32767); }

mcc::gamepad_state read_sdl(SDL_Gamepad* g) {
    mcc::gamepad_state s{};
    struct map {
        SDL_GamepadButton button;
        uint32_t bit;
    };
    static constexpr map buttons[] = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP, k_dpad_up},       {SDL_GAMEPAD_BUTTON_DPAD_DOWN, k_dpad_down},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, k_dpad_left},   {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, k_dpad_right},
        {SDL_GAMEPAD_BUTTON_START, k_start},           {SDL_GAMEPAD_BUTTON_BACK, k_back},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, k_lthumb},     {SDL_GAMEPAD_BUTTON_RIGHT_STICK, k_rthumb},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, k_lshoulder},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, k_rshoulder},
        {SDL_GAMEPAD_BUTTON_SOUTH, k_a},               {SDL_GAMEPAD_BUTTON_EAST, k_b},
        {SDL_GAMEPAD_BUTTON_WEST, k_x},                {SDL_GAMEPAD_BUTTON_NORTH, k_y},
    };
    for (const auto& m : buttons) {
        if (SDL_GetGamepadButton(g, m.button)) s.buttons |= m.bit;
    }
    s.thumb_lx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
    s.thumb_ly = invert(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY));  // SDL: abajo = +
    s.thumb_rx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX);
    s.thumb_ry = invert(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY));
    s.left_trigger = trigger(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    s.right_trigger = trigger(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    return s;
}

// Clave estable entre las ventanas del juego del mismo PC: ruta del dispositivo
// si existe; si no, GUID + número de serie + orden entre mandos idénticos.
uint64_t make_key(SDL_Gamepad* g, SDL_JoystickID id, std::map<uint64_t, int>& twins) {
    const char* path = SDL_GetGamepadPath(g);
    if (path && *path) return fnv1a(path, strlen(path));
    SDL_GUID guid = SDL_GetGamepadGUIDForID(id);
    uint64_t h = fnv1a(&guid, sizeof(guid));
    if (const char* serial = SDL_GetGamepadSerial(g)) h = fnv1a(serial, strlen(serial), h);
    int ordinal = twins[h]++;
    return fnv1a(&ordinal, sizeof(ordinal), h);
}

void rebuild_sdl_devices() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    std::vector<device> fresh;
    std::map<uint64_t, int> twins;
    for (int i = 0; i < count; ++i) {
        device d;
        d.id = ids[i];
        // Reutilizar el mando ya abierto si sigue conectado.
        for (auto& old : g_devices) {
            if (old.id == d.id && old.pad) {
                d.pad = old.pad;
                old.pad = nullptr;
                break;
            }
        }
        if (!d.pad) d.pad = SDL_OpenGamepad(d.id);
        if (!d.pad) continue;
        d.key = make_key(d.pad, d.id, twins);
        const char* name = SDL_GetGamepadName(d.pad);
        d.name = name ? name : "Mando";
        const char* kind = SDL_GetGamepadStringForType(SDL_GetGamepadType(d.pad));
        d.kind = kind ? kind : "?";
        fresh.push_back(std::move(d));
    }
    SDL_free(ids);
    for (auto& old : g_devices) {
        if (old.pad) SDL_CloseGamepad(old.pad);
    }
    g_devices = std::move(fresh);
    R8_INFO("Mandos detectados: %zu", g_devices.size());
    for (auto& d : g_devices) {
        R8_INFO("  - %s [%s] clave %016llX", d.name.c_str(), d.kind.c_str(),
                static_cast<unsigned long long>(d.key));
    }
}

void rebuild_xinput_devices() {
    std::vector<device> fresh;
    for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE st{};
        if (XInputGetState(static_cast<DWORD>(i), &st) != ERROR_SUCCESS) continue;
        device d;
        d.xinput_user = i;
        d.key = 0x5849000000000000ull | static_cast<uint64_t>(i);
        d.name = "Mando XInput " + std::to_string(i + 1);
        d.kind = "xinput";
        fresh.push_back(std::move(d));
    }
    if (fresh.size() != g_devices.size()) R8_INFO("Mandos XInput: %zu", fresh.size());
    g_devices = std::move(fresh);
}

device* find(uint64_t key) {
    for (auto& d : g_devices) {
        if (d.key == key) return &d;
    }
    return nullptr;
}

uint64_t parse_pad_key(const std::string& s) {
    if (s.rfind("pad:", 0) != 0) return 0;
    try {
        return std::stoull(s.substr(4), nullptr, 16);
    } catch (...) {
        return 0;
    }
}

// Con el mutex tomado.
void resolve_assignments() {
    auto s = config::get();
    int count = players::local_count();
    if (g_config_dirty.exchange(false)) {
        for (int p = 0; p < k_players; ++p) {
            const std::string& dev = s.players[p].device;
            g_slots[p].km = p == 0 && dev == "km";
            uint64_t wanted = parse_pad_key(dev);
            if (g_slots[p].key != 0 && (g_slots[p].km || (wanted != 0 && wanted != g_slots[p].key))) {
                instance::release(g_slots[p].key);
                g_slots[p].key = 0;
            }
        }
    }
    for (int p = 0; p < k_players; ++p) {
        player_slot& slot = g_slots[p];
        if (p >= count) {
            if (slot.key != 0) {
                instance::release(slot.key);
                slot.key = 0;
            }
            continue;
        }
        if (slot.km) continue;
        if (slot.key != 0 && find(slot.key) != nullptr) continue;  // sigue conectado
        uint64_t wanted = parse_pad_key(s.players[p].device);
        if (wanted != 0) {
            if (find(wanted) && instance::claim(wanted, p)) slot.key = wanted;
            continue;
        }
        if (s.players[p].device != "auto") continue;
        for (auto& d : g_devices) {
            bool taken = false;
            for (auto& other : g_slots) taken |= other.key == d.key;
            if (taken || instance::owner(d.key, nullptr, nullptr)) continue;
            if (instance::claim(d.key, p)) {
                slot.key = d.key;
                R8_INFO("Jugador %d -> %s", players::global_number(p), d.name.c_str());
                break;
            }
        }
    }
}

// Con el mutex tomado.
void handle_assign_mode() {
    int p = g_assigning.load();
    if (p < 0 || p >= k_players) return;
    for (auto& d : g_devices) {
        bool pressed_now = (d.state.buttons & k_a) && !(d.previous_buttons & k_a);
        if (!pressed_now) continue;
        int inst = -1, who = -1;
        bool owned = instance::owner(d.key, &inst, &who);
        bool mine = owned && inst == static_cast<int>(instance::index());
        if (owned && !(mine && who == p)) {
            if (mine) {
                // Cambio dentro de esta ventana: el otro jugador queda libre.
                g_slots[who].key = 0;
                instance::release(d.key);
            } else {
                continue;  // es de otra ventana del juego
            }
        }
        if (g_slots[p].key != 0) instance::release(g_slots[p].key);
        if (instance::claim(d.key, p)) {
            g_slots[p].key = d.key;
            g_slots[p].km = false;
            auto s = config::get();
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "pad:%016llX", static_cast<unsigned long long>(d.key));
            s.players[p].device = buffer;
            config::set(s);
            R8_INFO("Jugador %d asignado a %s", players::global_number(p), d.name.c_str());
        }
        g_assigning.store(-1);
        return;
    }
}

void update_menu_combo() {
    device* d = g_slots[0].key ? find(g_slots[0].key) : nullptr;
    if (!d) return;
    uint32_t combo = k_back | k_start;
    if ((d->state.buttons & combo) == combo && (d->previous_buttons & combo) != combo) {
        g_menu_combo.store(true);
    }
}

void apply_rumble() {
    for (auto& slot : g_slots) {
        if (!slot.rumble_dirty || slot.key == 0) continue;
        slot.rumble_dirty = false;
        device* d = find(slot.key);
        if (!d) continue;
        if (d->pad) {
            SDL_RumbleGamepad(d->pad, slot.rumble_low, slot.rumble_high, 250);
        } else if (d->xinput_user >= 0) {
            XINPUT_VIBRATION v{slot.rumble_low, slot.rumble_high};
            XInputSetState(static_cast<DWORD>(d->xinput_user), &v);
        }
    }
}

void thread_main() {
    auto s = config::get();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI, s.sdl_wgi ? "1" : "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, s.sdl_hidapi ? "1" : "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, s.sdl_rawinput ? "1" : "0");

    g_sdl = SDL_Init(SDL_INIT_GAMEPAD);
    if (g_sdl) {
        g_backend = "SDL3";
        R8_INFO("Mandos: SDL3 activo (XInput/WGI/HID)");
    } else {
        g_backend = "XInput (máx. 4)";
        R8_WARN("SDL3 no se pudo iniciar (%s): se usa XInput, máximo 4 mandos", SDL_GetError());
    }

    {
        std::lock_guard lock(g_mutex);
        if (g_sdl) rebuild_sdl_devices();
        else rebuild_xinput_devices();
    }

    int tick = 0;
    while (g_running.load()) {
        bool changed = false;
        if (g_sdl) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED)
                    changed = true;
            }
            SDL_UpdateGamepads();
        }
        {
            std::lock_guard lock(g_mutex);
            if (g_sdl && changed) rebuild_sdl_devices();
            if (!g_sdl && (tick % 250) == 0) rebuild_xinput_devices();  // cada ~1 s
            for (auto& d : g_devices) {
                d.previous_buttons = d.state.buttons;
                if (d.pad) {
                    d.state = read_sdl(d.pad);
                } else if (d.xinput_user >= 0) {
                    XINPUT_STATE st{};
                    if (XInputGetState(static_cast<DWORD>(d.xinput_user), &st) == ERROR_SUCCESS) {
                        d.state.buttons = st.Gamepad.wButtons;
                        d.state.left_trigger = st.Gamepad.bLeftTrigger;
                        d.state.right_trigger = st.Gamepad.bRightTrigger;
                        d.state.thumb_lx = st.Gamepad.sThumbLX;
                        d.state.thumb_ly = st.Gamepad.sThumbLY;
                        d.state.thumb_rx = st.Gamepad.sThumbRX;
                        d.state.thumb_ry = st.Gamepad.sThumbRY;
                    }
                }
            }
            if ((tick % 50) == 0 || changed) resolve_assignments();
            handle_assign_mode();
            update_menu_combo();
            apply_rumble();
        }
        ++tick;
        Sleep(4);  // ~250 Hz
    }

    std::lock_guard lock(g_mutex);
    for (auto& d : g_devices) {
        if (d.pad) SDL_CloseGamepad(d.pad);
    }
    g_devices.clear();
    if (g_sdl) SDL_Quit();
}

}  // namespace

bool initialize() {
    if (g_running.exchange(true)) return true;
    g_thread = std::thread(thread_main);
    return true;
}

void shutdown() {
    if (!g_running.exchange(false)) return;
    if (g_thread.joinable()) g_thread.join();
    instance::release_mine();
}

std::string backend_name() {
    std::lock_guard lock(g_mutex);
    return g_backend;
}

std::vector<device_info> devices() {
    std::lock_guard lock(g_mutex);
    std::vector<device_info> out;
    for (auto& d : g_devices) {
        device_info info{d.key, d.name, d.kind};
        instance::owner(d.key, &info.owner_instance, &info.owner_player);
        out.push_back(std::move(info));
    }
    return out;
}

bool player_uses_km(int player) {
    std::lock_guard lock(g_mutex);
    return player >= 0 && player < k_players && g_slots[player].km;
}

bool read(int player, mcc::gamepad_state* out) {
    if (player < 0 || player >= k_players) return false;
    std::lock_guard lock(g_mutex);
    if (g_slots[player].key == 0) return false;
    device* d = find(g_slots[player].key);
    if (!d) return false;
    *out = d->state;
    return true;
}

void rumble(int player, uint16_t low, uint16_t high) {
    if (player < 0 || player >= k_players) return;
    std::lock_guard lock(g_mutex);
    auto& slot = g_slots[player];
    if (slot.rumble_low != low || slot.rumble_high != high || low || high) {
        slot.rumble_low = low;
        slot.rumble_high = high;
        slot.rumble_dirty = true;
    }
}

void begin_assign(int player) { g_assigning.store(player); }
void cancel_assign() { g_assigning.store(-1); }
int assigning_player() { return g_assigning.load(); }

void unassign(int player) {
    if (player < 0 || player >= k_players) return;
    std::lock_guard lock(g_mutex);
    if (g_slots[player].key != 0) instance::release(g_slots[player].key);
    g_slots[player].key = 0;
}

void apply_config() { g_config_dirty.store(true); }

uint64_t assigned_key(int player) {
    if (player < 0 || player >= k_players) return 0;
    std::lock_guard lock(g_mutex);
    return g_slots[player].key;
}

bool menu_pad(mcc::gamepad_state* out) { return read(0, out); }

bool menu_combo_pressed() { return g_menu_combo.exchange(false); }

}  // namespace r8::input
