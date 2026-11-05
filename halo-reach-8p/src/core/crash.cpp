#include "core/crash.h"

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <string>

#include "core/log.h"
#include "core/paths.h"

namespace r8::crash {
namespace {

void* g_handler = nullptr;
uintptr_t g_module_base = 0;
uintptr_t g_module_end = 0;
std::atomic<const char*> g_breadcrumb{"inicio"};
std::atomic<bool> g_dumped{false};

bool is_fatal(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_PRIV_INSTRUCTION:
            return true;
        default:
            return false;
    }
}

LONG CALLBACK handler(EXCEPTION_POINTERS* info) {
    auto address = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    if (!is_fatal(info->ExceptionRecord->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
    if (address < g_module_base || address >= g_module_end) return EXCEPTION_CONTINUE_SEARCH;
    if (g_dumped.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;

    R8_ERROR("FALLO dentro de Reach8: código 0x%08lX en +0x%llX (última operación: %s)",
             info->ExceptionRecord->ExceptionCode,
             static_cast<unsigned long long>(address - g_module_base), g_breadcrumb.load());

    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[128];
    swprintf_s(name, L"\\reach8_%04u%02u%02u_%02u%02u%02u.dmp", t.wYear, t.wMonth, t.wDay,
               t.wHour, t.wMinute, t.wSecond);
    std::wstring path = paths::crash_dir() + name;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory |
                                                     MiniDumpWithThreadInfo),
                          &mei, nullptr, nullptr);
        CloseHandle(file);
        R8_ERROR("Volcado guardado en %s", paths::narrow(path).c_str());
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void install(void* own_module) {
    g_module_base = reinterpret_cast<uintptr_t>(own_module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_module_base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_module_base + dos->e_lfanew);
    g_module_end = g_module_base + nt->OptionalHeader.SizeOfImage;
    g_handler = AddVectoredExceptionHandler(1, handler);
}

void uninstall() {
    if (g_handler) {
        RemoveVectoredExceptionHandler(g_handler);
        g_handler = nullptr;
    }
}

void breadcrumb(const char* what) { g_breadcrumb.store(what); }

}  // namespace r8::crash
