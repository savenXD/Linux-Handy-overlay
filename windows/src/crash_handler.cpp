#include "crash_handler.h"
#include "common.h"
#include <cstdint>
#include <dbghelp.h>
#include <algorithm>
#include <string>
#include <vector>

namespace cronq {
namespace crash_handler {
namespace {
wchar_t g_crashDir[MAX_PATH * 2] = {};
// dbghelp is loaded up front: loading a DLL inside a crash is not safe.
using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
MiniDumpWriteDumpFn g_writeDump = nullptr;
constexpr size_t kKeepDumps = 3;
volatile LONG g_reporting = 0;
// Fixed storage and Win32 writes: the faulting thread may already hold CRT/logging locks.
struct Report {
    char data[4096]{};
    DWORD size = 0;
    void Text(const char* s) {
        while (*s && size < sizeof(data)) data[size++] = *s++;
    }
    void Number(uint64_t n, unsigned base = 16, unsigned width = 0) {
        char digits[64];
        unsigned used = 0;
        do {
            digits[used++] = "0123456789ABCDEF"[n % base];
            n /= base;
        } while (n && used < sizeof(digits));
        while (used < width && used < sizeof(digits)) digits[used++] = '0';
        while (used && size < sizeof(data)) data[size++] = digits[--used];
    }
    void Field(const char* name, uint64_t value) {
        Text(name);
        Text("0x");
        Number(value);
        Text("\r\n");
    }
};
Report g_stamp, g_report;
wchar_t g_reportPath[MAX_PATH * 2 + 80]{};
wchar_t g_modulePath[MAX_PATH]{};
char g_moduleUtf8[MAX_PATH * 3]{};

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord || InterlockedExchange(&g_reporting, 1)) return EXCEPTION_CONTINUE_SEARCH;
    SYSTEMTIME t{};
    GetLocalTime(&t);
    Report& stamp = g_stamp;
    stamp.Number(t.wYear, 10, 4);
    stamp.Number(t.wMonth, 10, 2);
    stamp.Number(t.wDay, 10, 2);
    stamp.Text("-");
    stamp.Number(t.wHour, 10, 2);
    stamp.Number(t.wMinute, 10, 2);
    stamp.Number(t.wSecond, 10, 2);
    stamp.Text("-");
    stamp.Number(GetCurrentProcessId(), 10);
    stamp.Text(".log");
    auto& path = g_reportPath;
    size_t pos = 0;
    while (g_crashDir[pos] && pos < MAX_PATH * 2 - 1) {
        path[pos] = g_crashDir[pos];
        ++pos;
    }
    if (pos) path[pos++] = L'\\';
    const char* prefix = "crash-";
    while (*prefix) path[pos++] = *prefix++;
    for (DWORD i = 0; i < stamp.size && pos < sizeof(path) / sizeof(path[0]) - 1; ++i)
        path[pos++] = stamp.data[i];
    Report& r = g_report;
    r.Text("Cronq crash report\r\n");
    r.Field("Exception: ", ep->ExceptionRecord->ExceptionCode);
    r.Field("Flags: ", ep->ExceptionRecord->ExceptionFlags);
    const auto address = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
    r.Field("Address: ", address);
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(ep->ExceptionRecord->ExceptionAddress, &memory, sizeof(memory))) {
        r.Field("Module base: ", reinterpret_cast<uintptr_t>(memory.AllocationBase));
        r.Field("Module offset: ", address - reinterpret_cast<uintptr_t>(memory.AllocationBase));
        auto& module = g_modulePath;
        GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase), module, MAX_PATH);
        auto& utf8 = g_moduleUtf8;
        int bytes = WideCharToMultiByte(CP_UTF8, 0, module, -1, utf8, sizeof(utf8), nullptr, nullptr);
        if (bytes > 0) {
            r.Text("Module: ");
            r.Text(utf8);
            r.Text("\r\n");
        }
    }
#if defined(_M_X64) || defined(__x86_64__)
    if (ep->ContextRecord) {
        const auto& c = *ep->ContextRecord;
        r.Field("RIP: ", c.Rip);
        r.Field("RSP: ", c.Rsp);
        r.Field("RBP: ", c.Rbp);
        r.Field("RAX: ", c.Rax);
        r.Field("RBX: ", c.Rbx);
        r.Field("RCX: ", c.Rcx);
        r.Field("RDX: ", c.Rdx);
        r.Field("RSI: ", c.Rsi);
        r.Field("RDI: ", c.Rdi);
        r.Field("R8: ", c.R8);
        r.Field("R9: ", c.R9);
        r.Field("R10: ", c.R10);
        r.Field("R11: ", c.R11);
        r.Field("R12: ", c.R12);
        r.Field("R13: ", c.R13);
        r.Field("R14: ", c.R14);
        r.Field("R15: ", c.R15);
    }
#endif
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, r.data, r.size, &written, nullptr);
        CloseHandle(file);
    }
    // A small minidump (stacks, threads, modules; no full heap) next to the text report.
    if (g_writeDump) {
        size_t len = 0;
        while (path[len]) ++len;
        if (len > 4) {
            path[len - 3] = L'd';
            path[len - 2] = L'm';
            path[len - 1] = L'p';
            HANDLE dump = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (dump != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION info{};
                info.ThreadId = GetCurrentThreadId();
                info.ExceptionPointers = ep;
                info.ClientPointers = FALSE;
                g_writeDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                            static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
                            &info, nullptr, nullptr);
                CloseHandle(dump);
            }
        }
    }
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (err && err != INVALID_HANDLE_VALUE) WriteFile(err, r.data, r.size, &written, nullptr);
    return EXCEPTION_EXECUTE_HANDLER;
}
// Dumps are a few MB each: keep only the newest few.
void PruneDumps(const std::wstring& dir) {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\crash-*.dmp").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do names.push_back(fd.cFileName);
    while (FindNextFileW(find, &fd));
    FindClose(find);
    std::sort(names.begin(), names.end());  // crash-YYYYMMDD-HHMMSS-pid: oldest first
    for (size_t i = 0; i + kKeepDumps < names.size(); ++i) DeleteFileW((dir + L"\\" + names[i]).c_str());
}
}  // namespace
void Install() {
    const auto dir = DataDir();
    size_t i = 0;
    while (i < sizeof(g_crashDir) / sizeof(g_crashDir[0]) - 1 && i < dir.size()) {
        g_crashDir[i] = dir[i];
        ++i;
    }
    g_crashDir[i] = 0;
    if (HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
        g_writeDump = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    PruneDumps(dir);
    SetUnhandledExceptionFilter(CrashFilter);
    Log("CRASH", "Crash handler registered (logs in '%s').", Narrow(g_crashDir).c_str());
}
}  // namespace crash_handler
}  // namespace cronq
