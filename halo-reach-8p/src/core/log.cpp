#include "core/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

namespace r8::log {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
std::deque<std::string> g_ring;
constexpr size_t k_ring_size = 300;

const char* level_name(level lvl) {
    switch (lvl) {
        case level::warn: return "AVISO";
        case level::error: return "ERROR";
        default: return "INFO ";
    }
}

}  // namespace

void initialize(const std::wstring& file_path) {
    std::lock_guard lock(g_mutex);
    if (g_file == nullptr) {
        _wfopen_s(&g_file, file_path.c_str(), L"w, ccs=UTF-8");
        // Las líneas escritas antes de abrir el archivo también se guardan.
        if (g_file != nullptr) {
            for (const auto& line : g_ring) {
                wchar_t wide[1200];
                if (MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, wide, 1200) > 0)
                    fwprintf(g_file, L"%s\n", wide);
            }
            fflush(g_file);
        }
    }
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    if (g_file != nullptr) {
        fclose(g_file);
        g_file = nullptr;
    }
}

void write(level lvl, const char* fmt, ...) {
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    SYSTEMTIME t;
    GetLocalTime(&t);

    char line[1200];
    snprintf(line, sizeof(line), "%02u:%02u:%02u.%03u [%s] %s", t.wHour, t.wMinute, t.wSecond,
             t.wMilliseconds, level_name(lvl), message);

    OutputDebugStringA(line);
    OutputDebugStringA("\n");

    std::lock_guard lock(g_mutex);
    g_ring.emplace_back(line);
    if (g_ring.size() > k_ring_size) {
        g_ring.pop_front();
    }
    if (g_file != nullptr) {
        // El archivo está abierto en modo UTF-8: se escribe convirtiendo desde UTF-8.
        wchar_t wide[1200];
        int n = MultiByteToWideChar(CP_UTF8, 0, line, -1, wide, 1200);
        if (n > 0) {
            fwprintf(g_file, L"%s\n", wide);
            fflush(g_file);
        }
    }
}

std::vector<std::string> tail(size_t max_lines) {
    std::lock_guard lock(g_mutex);
    size_t start = g_ring.size() > max_lines ? g_ring.size() - max_lines : 0;
    return {g_ring.begin() + static_cast<std::ptrdiff_t>(start), g_ring.end()};
}

}  // namespace r8::log
