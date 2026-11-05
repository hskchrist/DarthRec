#pragma once

#include <string>
#include <vector>

namespace r8::log {

enum class level { info, warn, error };

void initialize(const std::wstring& file_path);
void shutdown();

void write(level lvl, const char* fmt, ...);

// Últimas líneas para mostrarlas en el menú de diagnóstico.
std::vector<std::string> tail(size_t max_lines);

}  // namespace r8::log

#define R8_INFO(...) ::r8::log::write(::r8::log::level::info, __VA_ARGS__)
#define R8_WARN(...) ::r8::log::write(::r8::log::level::warn, __VA_ARGS__)
#define R8_ERROR(...) ::r8::log::write(::r8::log::level::error, __VA_ARGS__)
