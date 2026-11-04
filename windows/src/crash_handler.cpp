#include "crash_handler.h"
#include "common.h"
#include <cstdint>

namespace cronq {
namespace crash_handler {
namespace {
wchar_t g_crashDir[MAX_PATH * 2] = {};
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
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (err && err != INVALID_HANDLE_VALUE) WriteFile(err, r.data, r.size, &written, nullptr);
    return EXCEPTION_EXECUTE_HANDLER;
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
    SetUnhandledExceptionFilter(CrashFilter);
    Log("CRASH", "Crash handler registered (logs in '%s').", Narrow(g_crashDir).c_str());
}
}  // namespace crash_handler
}  // namespace cronq
