#include "crash_handler.h"
#include "common.h"
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace cronq;
int main(int argc, char**) {
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    if (argc > 1) {
        crash_handler::Install();
        RaiseException(0xE0421234, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return 2;
    }
    wchar_t path[MAX_PATH * 4]{};
    GetModuleFileNameW(nullptr, path, sizeof(path) / sizeof(path[0]));
    std::wstring command = L"\"" + std::wstring(path) + L"\" --fault";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &si, &pi)) {
        std::puts("FAIL: spawn");
        return 1;
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, 10000), code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    if (wait != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    const auto pattern = DataDir() + L"\\crash-*-" + std::to_wstring(pi.dwProcessId) + L".log";
    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    if (search == INVALID_HANDLE_VALUE || wait != WAIT_OBJECT_0 || code == 0) {
        std::puts("FAIL: fault did not produce crash report");
        return 1;
    }
    FindClose(search);
    const auto report = DataDir() + L"\\" + found.cFileName;
    std::ifstream f(report.c_str(), std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(f)), {});
    f.close();
    DeleteFileW(report.c_str());
    if (text.find("E0421234") == std::string::npos || text.find("Module offset:") == std::string::npos ||
        text.find("RIP:") == std::string::npos) {
        std::puts("FAIL: missing diagnostics");
        return 1;
    }
    std::wstring dump = report.substr(0, report.size() - 3) + L"dmp";
    WIN32_FILE_ATTRIBUTE_DATA dumpInfo{};
    const bool haveDump = GetFileAttributesExW(dump.c_str(), GetFileExInfoStandard, &dumpInfo) &&
                          (dumpInfo.nFileSizeLow || dumpInfo.nFileSizeHigh);
    DeleteFileW(dump.c_str());
    if (!haveDump) {
        std::puts("FAIL: missing minidump");
        return 1;
    }
    std::puts("crash_report_test: all checks passed");
    return 0;
}
