#include "core/paths.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>

namespace r8::paths {

const std::wstring& base_dir() {
    static const std::wstring dir = [] {
        std::wstring result = L".\\Reach8";
        PWSTR documents = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) {
            result = std::wstring(documents) + L"\\Reach8";
            CoTaskMemFree(documents);
        }
        CreateDirectoryW(result.c_str(), nullptr);
        CreateDirectoryW((result + L"\\logs").c_str(), nullptr);
        CreateDirectoryW((result + L"\\crash").c_str(), nullptr);
        return result;
    }();
    return dir;
}

std::wstring config_file() { return base_dir() + L"\\reach8.ini"; }

std::wstring log_file(unsigned instance) {
    return base_dir() + L"\\logs\\reach8_instancia" + std::to_wstring(instance + 1) + L".log";
}

std::wstring crash_dir() { return base_dir() + L"\\crash"; }

std::wstring report_file() { return base_dir() + L"\\diagnostico.txt"; }

std::wstring exe_path() {
    wchar_t buffer[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return buffer;
}

std::wstring exe_name_lower() {
    std::wstring path = exe_path();
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return name;
}

std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0,
                                nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

}  // namespace r8::paths
