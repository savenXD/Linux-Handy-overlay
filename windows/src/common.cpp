#include "common.h"
#include "log_rotate.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace cronq {

std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

bool EqNoCase(const std::string& a, const char* b) {
    size_t n = strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) b++;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
    return s.substr(b, e - b);
}

static std::wstring EnvVar(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(name, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return std::wstring();
    return std::wstring(buf, n);
}

static std::wstring EnsureAppDir(const wchar_t* envName, const wchar_t* fallbackSub) {
    std::wstring base = EnvVar(envName);
    if (base.empty()) {
        std::wstring profile = EnvVar(L"USERPROFILE");
        base = profile.empty() ? std::wstring(L".") : profile + fallbackSub;
    }
    std::wstring dir = base + L"\\cronq";
    CreateDirectoryW(dir.c_str(),
                     nullptr);  // parent (AppData\Local etc.) always exists; ignore "already exists"
    return dir;
}

std::wstring DataDir() {
    static std::wstring dir = EnsureAppDir(L"LOCALAPPDATA", L"\\AppData\\Local");
    return dir;
}

std::wstring ConfigDir() {
    static std::wstring dir = EnsureAppDir(L"APPDATA", L"\\AppData\\Roaming");
    return dir;
}

int64_t NowUs() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (int64_t)((double)c.QuadPart * 1000000.0 / (double)freq.QuadPart);
}

namespace {

struct LogState {
    CRITICAL_SECTION cs;
    HANDLE file = INVALID_HANDLE_VALUE;
    LogState() {
        InitializeCriticalSection(&cs);
        std::wstring path = DataDir() + L"\\cronq.log";
        // Keep the log from growing forever: rotate past 2 MB.
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
            log_rotate::ShouldRotate((static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow)) {
            std::wstring old = DataDir() + L"\\cronq.log.old";
            MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    ~LogState() {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        DeleteCriticalSection(&cs);
    }
};

void Emit(const char* tag, bool isErr, const char* fmt, va_list args) {
    static LogState state;

    char msg[2048];
    vsnprintf(msg, sizeof(msg), fmt, args);
    msg[sizeof(msg) - 1] = '\0';

    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[2300];
    int n = snprintf(line, sizeof(line), "[%02d:%02d:%02d] [%s%s] %s\n", t.wHour, t.wMinute, t.wSecond, tag,
                     isErr ? " ERROR" : "", msg);
    if (n <= 0) return;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;

    EnterCriticalSection(&state.cs);
    if (state.file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(state.file, line, (DWORD)n, &written, nullptr);
    }
    LeaveCriticalSection(&state.cs);

    // Console build: also show it live. Harmless when no console is attached.
    fwrite(line, 1, (size_t)n, isErr ? stderr : stdout);
    fflush(isErr ? stderr : stdout);
}

}  // namespace

void Log(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Emit(tag, false, fmt, args);
    va_end(args);
}

void LogErr(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Emit(tag, true, fmt, args);
    va_end(args);
}

}  // namespace cronq
