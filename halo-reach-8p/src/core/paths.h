#pragma once

#include <string>

namespace r8::paths {

// Documentos\Reach8 (se crea si no existe).
const std::wstring& base_dir();
std::wstring config_file();
std::wstring log_file(unsigned instance);
std::wstring crash_dir();
std::wstring report_file();

// Ruta y nombre del ejecutable actual.
std::wstring exe_path();
std::wstring exe_name_lower();

std::string narrow(const std::wstring& s);
std::wstring widen(const std::string& s);

}  // namespace r8::paths
