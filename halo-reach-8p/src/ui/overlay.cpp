#include "ui/overlay.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

#include "core/config.h"
#include "core/log.h"
#include "core/paths.h"
#include "input/input.h"
#include "instance/instance.h"
#include "mcc/mcc.h"
#include "netprobe/netprobe.h"
#include "players/players.h"
#include "reach/reach_patches.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam,
                                                             LPARAM lparam);

namespace r8::overlay {
namespace {

struct queued_message {
    UINT msg;
    WPARAM wparam;
    LPARAM lparam;
};

std::atomic<bool> g_visible{false};
bool g_initialized = false;
bool g_failed = false;
HWND g_hwnd = nullptr;
WNDPROC g_original_wndproc = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;

std::mutex g_queue_mutex;
std::vector<queued_message> g_queue;

std::mutex g_toast_mutex;
std::string g_toast;
std::chrono::steady_clock::time_point g_toast_until;

bool g_key_was_down = false;
char g_names[config::k_max_local_players][32] = {};
bool g_names_loaded = false;

bool is_input_message(UINT msg) {
    return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) ||
           msg == WM_SETFOCUS || msg == WM_KILLFOCUS;
}

// Procedimiento de ventana propio: los mensajes se guardan para ImGui y se
// procesan en el hilo de render (ImGui no es seguro entre hilos).
LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == instance::k_layout_message) {
        instance::apply_layout(hwnd);
        return 0;
    }
    if (g_visible.load() && is_input_message(msg)) {
        std::lock_guard lock(g_queue_mutex);
        if (g_queue.size() < 512) g_queue.push_back({msg, wparam, lparam});
    }
    return CallWindowProcW(g_original_wndproc, hwnd, msg, wparam, lparam);
}

bool init(IDXGISwapChain* swap_chain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swap_chain->GetDesc(&desc)) || desc.OutputWindow == nullptr) return false;
    if (FAILED(swap_chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device))))
        return false;
    g_device->GetImmediateContext(&g_context);
    g_hwnd = desc.OutputWindow;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    wchar_t fonts[MAX_PATH] = {};
    GetWindowsDirectoryW(fonts, MAX_PATH);
    std::string segoe = paths::narrow(fonts) + "\\Fonts\\segoeui.ttf";
    if (GetFileAttributesA(segoe.c_str()) != INVALID_FILE_ATTRIBUTES) {
        io.Fonts->AddFontFromFileTTF(segoe.c_str(), 19.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;

    if (!ImGui_ImplWin32_Init(g_hwnd) || !ImGui_ImplDX11_Init(g_device, g_context)) return false;

    g_original_wndproc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndproc)));
    instance::set_window(g_hwnd);

    auto s = config::get();
    if (s.window_layout != "none") instance::request_layout(s.window_layout, s.window_monitor);

    R8_INFO("Menú listo (tecla 0x%X o Back+Start en el mando del jugador 1)", s.menu_key);
    return true;
}

void feed_gamepad(ImGuiIO& io) {
    mcc::gamepad_state pad{};
    if (!input::menu_pad(&pad)) return;
    auto key = [&](ImGuiKey k, uint32_t bit) { io.AddKeyEvent(k, (pad.buttons & bit) != 0); };
    key(ImGuiKey_GamepadFaceDown, 0x1000);
    key(ImGuiKey_GamepadFaceRight, 0x2000);
    key(ImGuiKey_GamepadFaceLeft, 0x4000);
    key(ImGuiKey_GamepadFaceUp, 0x8000);
    key(ImGuiKey_GamepadDpadUp, 0x0001);
    key(ImGuiKey_GamepadDpadDown, 0x0002);
    key(ImGuiKey_GamepadDpadLeft, 0x0004);
    key(ImGuiKey_GamepadDpadRight, 0x0008);
    key(ImGuiKey_GamepadL1, 0x0100);
    key(ImGuiKey_GamepadR1, 0x0200);
    auto axis = [&](ImGuiKey k, float v) {
        constexpr float dead = 0.3f;
        io.AddKeyAnalogEvent(k, v > dead, v > dead ? v : 0.0f);
    };
    axis(ImGuiKey_GamepadLStickLeft, -pad.thumb_lx / 32767.0f);
    axis(ImGuiKey_GamepadLStickRight, pad.thumb_lx / 32767.0f);
    axis(ImGuiKey_GamepadLStickUp, pad.thumb_ly / 32767.0f);
    axis(ImGuiKey_GamepadLStickDown, -pad.thumb_ly / 32767.0f);
}

void load_names() {
    auto s = config::get();
    for (int i = 0; i < config::k_max_local_players; ++i) {
        snprintf(g_names[i], sizeof(g_names[i]), "%s", s.players[i].name.c_str());
    }
    g_names_loaded = true;
}

std::string device_label(int player) {
    if (player == 0 && input::player_uses_km(0)) return "Teclado y ratón";
    uint64_t key = input::assigned_key(player);
    if (key == 0) return "(sin mando)";
    for (auto& d : input::devices()) {
        if (d.key == key) return d.name;
    }
    return "(desconectado)";
}

void tab_players() {
    auto s = config::get();
    ImGui::TextWrapped("Elige cuántos jugadores juegan en ESTA ventana ANTES de iniciar la "
                       "misión. Si ya estás dentro, reinicia la misión desde el menú de pausa.");
    int count = players::local_count();
    if (ImGui::SliderInt("Jugadores en esta ventana", &count, 1, config::k_max_local_players)) {
        players::set_local_count(count);
        s.local_players = count;
        config::set(s);
        input::apply_config();
    }
    ImGui::Separator();

    int assigning = input::assigning_player();
    for (int p = 0; p < count; ++p) {
        ImGui::PushID(p);
        ImGui::Text("Jugador %d", players::global_number(p));
        ImGui::SameLine(130);
        ImGui::SetNextItemWidth(220);
        if (ImGui::InputText("##nombre", g_names[p], sizeof(g_names[p]))) {
            s = config::get();
            s.players[p].name = g_names[p];
            config::set(s);
            players::reload();
        }
        ImGui::SameLine();
        if (assigning == p) {
            ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Pulsa A en el mando de este jugador...");
            ImGui::SameLine();
            if (ImGui::Button("Cancelar")) input::cancel_assign();
        } else {
            ImGui::Text("%s", device_label(p).c_str());
            ImGui::SameLine(640);
            if (ImGui::Button("Asignar mando")) input::begin_assign(p);
            ImGui::SameLine();
            if (p == 0 && ImGui::Button("Teclado/ratón")) {
                s = config::get();
                s.players[0].device = "km";
                config::set(s);
                input::unassign(0);
                input::apply_config();
            }
            if (p == 0) ImGui::SameLine();
            if (ImGui::Button("Auto")) {
                s = config::get();
                s.players[p].device = "auto";
                config::set(s);
                input::unassign(p);
                input::apply_config();
            }
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::Button("Guardar configuración")) {
        config::save();
        toast("Configuración guardada");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Documentos\\Reach8\\reach8.ini");
}

void tab_devices() {
    ImGui::Text("Lector de mandos: %s", input::backend_name().c_str());
    ImGui::Text("Ventanas del juego con Reach8 abiertas: %d", instance::alive_count());
    ImGui::Separator();
    auto list = input::devices();
    if (list.empty()) ImGui::TextDisabled("No se detecta ningún mando.");
    if (ImGui::BeginTable("mandos", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Mando");
        ImGui::TableSetupColumn("Tipo");
        ImGui::TableSetupColumn("Asignado a");
        ImGui::TableHeadersRow();
        for (auto& d : list) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", d.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", d.kind.c_str());
            ImGui::TableNextColumn();
            if (d.owner_instance < 0) ImGui::TextDisabled("libre");
            else ImGui::Text("Ventana %d - Jugador %d", d.owner_instance + 1,
                             d.owner_instance * 4 + d.owner_player + 1);
        }
        ImGui::EndTable();
    }
}

void tab_window() {
    auto s = config::get();
    ImGui::TextWrapped("Pon el juego en modo ventana. Para 8 jugadores en un PC se usan 2 "
                       "ventanas del juego: una a la izquierda y otra a la derecha.");
    ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1),
                       "Aviso: unir las 2 ventanas en la MISMA campaña aún no está "
                       "implementado (ver LIMITACIONES).");
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Monitor (0 = principal)", &s.window_monitor);
    const char* layouts[][2] = {{"left", "Mitad izquierda"}, {"right", "Mitad derecha"},
                                {"top", "Mitad superior"},   {"bottom", "Mitad inferior"},
                                {"full", "Pantalla completa sin bordes"}};
    for (auto& l : layouts) {
        if (ImGui::Button(l[1])) {
            s.window_layout = l[0];
            config::set(s);
            instance::request_layout(l[0], s.window_monitor);
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    if (ImGui::Button("Guardar disposición")) {
        config::set(s);
        config::save();
        toast("Disposición guardada: se aplicará al abrir el juego");
    }
}

void tab_diagnostics() {
    ImGui::TextUnformatted(diagnostics_text().c_str());
    if (ImGui::Button("Guardar informe")) {
        write_report();
        toast("Informe guardado en Documentos\\Reach8\\diagnostico.txt");
    }
    ImGui::Separator();
    ImGui::Text("Registro:");
    ImGui::BeginChild("log", ImVec2(0, 220), ImGuiChildFlags_Borders);
    for (auto& line : log::tail(120)) ImGui::TextUnformatted(line.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void tab_about() {
    ImGui::Text("Reach8 %s - pantalla dividida para Halo: Reach (MCC)", R8_VERSION);
    ImGui::TextWrapped("Usa este mod SOLO con MCC iniciado en modo \"Anti-Cheat Disabled\". "
                       "Reach8 no toca Easy Anti-Cheat ni ningún archivo del juego.");
    ImGui::TextWrapped("Basado en la investigación de AlphaRing (WinterSquire, MIT) y libmcc "
                       "(SpringContingency).");
}

void draw_menu() {
    ImGui::SetNextWindowSize(ImVec2(900, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
    char title[64];
    snprintf(title, sizeof(title), "Reach8 - Ventana %u###reach8", instance::index() + 1);
    bool open = true;
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Jugadores")) { tab_players(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Mandos")) { tab_devices(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Ventana")) { tab_window(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Diagnóstico")) { tab_diagnostics(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Acerca de")) { tab_about(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    if (!open) g_visible.store(false);
}

void draw_toast() {
    std::lock_guard lock(g_toast_mutex);
    if (g_toast.empty() || std::chrono::steady_clock::now() > g_toast_until) return;
    ImGui::SetNextWindowPos(ImVec2(20, 20));
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("##toast", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav);
    ImGui::TextUnformatted(g_toast.c_str());
    ImGui::End();
}

bool toast_active() {
    std::lock_guard lock(g_toast_mutex);
    return !g_toast.empty() && std::chrono::steady_clock::now() <= g_toast_until;
}

void handle_toggle() {
    int key = config::get().menu_key;
    bool down = (GetAsyncKeyState(key) & 0x8000) != 0 && GetForegroundWindow() == g_hwnd;
    bool pressed = down && !g_key_was_down;
    g_key_was_down = down;
    if (pressed || input::menu_combo_pressed()) {
        bool now = !g_visible.load();
        g_visible.store(now);
        if (now) load_names();
        else input::cancel_assign();
    }
}

}  // namespace

void on_present(IDXGISwapChain* swap_chain) {
    if (g_failed || swap_chain == nullptr) return;
    if (!g_initialized) {
        if (!init(swap_chain)) {
            g_failed = true;
            R8_ERROR("No se pudo crear el menú en pantalla (el resto del mod sigue activo)");
            return;
        }
        g_initialized = true;
        toast("Reach8 cargado - F8 o Back+Start para abrir el menú", 10.0f);
    }

    handle_toggle();
    bool visible = g_visible.load();
    if (!visible && !toast_active()) {
        std::lock_guard lock(g_queue_mutex);
        g_queue.clear();
        return;
    }
    if (!g_names_loaded) load_names();

    {
        std::vector<queued_message> pending;
        {
            std::lock_guard lock(g_queue_mutex);
            pending.swap(g_queue);
        }
        for (auto& m : pending) ImGui_ImplWin32_WndProcHandler(g_hwnd, m.msg, m.wparam, m.lparam);
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = visible;
    if (visible) feed_gamepad(io);
    ImGui::NewFrame();
    if (visible) draw_menu();
    draw_toast();
    ImGui::Render();

    ID3D11Texture2D* back_buffer = nullptr;
    if (FAILED(swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                     reinterpret_cast<void**>(&back_buffer))))
        return;
    ID3D11RenderTargetView* rtv = nullptr;
    HRESULT hr = g_device->CreateRenderTargetView(back_buffer, nullptr, &rtv);
    back_buffer->Release();
    if (FAILED(hr)) return;

    ID3D11RenderTargetView* old_rtv = nullptr;
    ID3D11DepthStencilView* old_dsv = nullptr;
    g_context->OMGetRenderTargets(1, &old_rtv, &old_dsv);
    g_context->OMSetRenderTargets(1, &rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_context->OMSetRenderTargets(1, &old_rtv, old_dsv);
    if (old_rtv) old_rtv->Release();
    if (old_dsv) old_dsv->Release();
    rtv->Release();
}

bool menu_open() { return g_visible.load(std::memory_order_relaxed); }

void toast(const std::string& message, float seconds) {
    std::lock_guard lock(g_toast_mutex);
    g_toast = message;
    g_toast_until = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(static_cast<int>(seconds * 1000));
}

std::string diagnostics_text() {
    const auto& st = mcc::get_status();
    std::ostringstream o;
    o << "Reach8 " << R8_VERSION << "\n";
    o << "MCC: " << st.exe_version << " ("
      << (st.which == mcc::store::steam      ? "Steam"
          : st.which == mcc::store::winstore ? "Microsoft Store"
                                             : "?")
      << ")\n";
    char rva[96];
    snprintf(rva, sizeof(rva), "i_game_manager: tabla +0x%llX, objeto +0x%llX",
             static_cast<unsigned long long>(st.vtable_rva),
             static_cast<unsigned long long>(st.game_manager_rva));
    o << rva << " (" << st.method << ")\n";
    o << "Perfil de build: " << mcc::profile_name(st.profile) << "\n";
    o << "Ganchos MCC: " << (st.hooks_ok ? "activos" : "INACTIVOS") << "\n";
    o << "Usuario principal identificado: " << (mcc::primary_xuid() != 0 ? "sí" : "todavía no")
      << "\n";
    o << "Ventana " << instance::index() + 1 << " de " << instance::alive_count()
      << " | jugadores locales: " << players::local_count() << "\n";
    o << "haloreach.dll: " << (reach::loaded() ? reach::dll_version() : std::string("no cargado"))
      << "\n";
    for (auto& p : reach::status()) {
        char line[200];
        snprintf(line, sizeof(line), "  - %s @+0x%llX: %s %s%s%s", p.name.c_str(),
                 static_cast<unsigned long long>(p.rva), p.state.c_str(),
                 p.original.empty() ? "" : "(original ", p.original.c_str(),
                 p.original.empty() ? "" : ")");
        o << line << "\n";
    }
    o << "Mandos (" << input::backend_name() << "): " << input::devices().size() << "\n";
    if (netprobe::installed()) {
        auto c = netprobe::snapshot();
        for (int i = 0; i < 4; ++i) {
            o << "  red " << netprobe::slot_name(i) << ": " << c.calls[i] << " llamadas, "
              << c.bytes[i] << " bytes\n";
        }
    }
    return o.str();
}

void write_report() {
    std::ofstream f(paths::report_file(), std::ios::trunc);
    f << diagnostics_text() << "\n--- Registro ---\n";
    for (auto& line : log::tail(300)) f << line << "\n";
}

void shutdown() {
    g_visible.store(false);
    if (g_hwnd && g_original_wndproc && IsWindow(g_hwnd)) {
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original_wndproc));
    }
}

}  // namespace r8::overlay
