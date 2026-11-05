#include "ipc.h"

#include <sddl.h>

namespace cronq {
namespace ipc {

namespace {

#ifndef PIPE_REJECT_REMOTE_CLIENTS
#define PIPE_REJECT_REMOTE_CLIENTS 0x00000008
#endif

constexpr DWORD kMaxLine = 131072;
constexpr DWORD kClientTimeoutMs = 2000;  // how long a connected client may take to send its line
constexpr DWORD kUiTimeoutMs = 8000;      // how long the UI thread may take to answer

HANDLE g_thread = nullptr;
HANDLE g_stopEvent = nullptr;
HANDLE g_firstPipe = INVALID_HANDLE_VALUE;
PVOID volatile g_target = nullptr;

std::wstring PipeName() {
    wchar_t user[256] = L"user";
    DWORD n = 256;
    if (!GetUserNameW(user, &n)) lstrcpyW(user, L"user");
    std::wstring name = L"\\\\.\\pipe\\cronq-";
    for (wchar_t* p = user; *p; ++p) name += (*p == L'\\' || *p == L'/') ? L'_' : *p;
    // Pipe names are machine-wide: the same account signed in twice (console + Remote Desktop)
    // must get one Cronq per session, not fight over a single pipe.
    DWORD session = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &session)) name += L"-" + std::to_wstring(session);
    return name;
}

// Only this user account (and SYSTEM) may open the pipe; other local users cannot send commands.
struct PipeSecurity {
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    PSECURITY_DESCRIPTOR sd = nullptr;
    PipeSecurity() {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return;
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::string buffer(size, '\0');
        LPWSTR sid = nullptr;
        if (size && GetTokenInformation(token, TokenUser, &buffer[0], size, &size) &&
            ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(&buffer[0])->User.Sid, &sid)) {
            std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")(A;;GA;;;SY)";
            if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr))
                sa.lpSecurityDescriptor = sd;
            LocalFree(sid);
        }
        CloseHandle(token);
    }
    ~PipeSecurity() {
        if (sd) LocalFree(sd);
    }
    SECURITY_ATTRIBUTES* Get() { return sd ? &sa : nullptr; }
};
PipeSecurity& Security() {
    static PipeSecurity security;
    return security;
}

// Overlapped read/write with a timeout. Returns false on error, timeout or end of stream.
bool PipeIo(HANDLE pipe, bool write, void* buf, DWORD len, DWORD* done, DWORD timeoutMs, HANDLE abortEvent) {
    OVERLAPPED ov;
    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;

    BOOL ok = write ? WriteFile(pipe, buf, len, nullptr, &ov) : ReadFile(pipe, buf, len, nullptr, &ov);
    if (!ok && GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(ov.hEvent);
        return false;
    }

    HANDLE waits[2] = {ov.hEvent, abortEvent};
    DWORD count = abortEvent ? 2 : 1;
    DWORD w = WaitForMultipleObjects(count, waits, FALSE, timeoutMs);
    if (w != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &ov);
        DWORD ignored = 0;
        GetOverlappedResult(pipe, &ov, &ignored, TRUE);
        CloseHandle(ov.hEvent);
        return false;
    }
    DWORD n = 0;
    BOOL r = GetOverlappedResult(pipe, &ov, &n, FALSE);
    CloseHandle(ov.hEvent);
    if (!r) return false;
    *done = n;
    return true;
}

std::string ExecuteOnUiThread(const std::string& line) {
    HWND target = static_cast<HWND>(InterlockedCompareExchangePointer(&g_target, nullptr, nullptr));
    if (!target) return "STARTING\n";

    Request* r = new Request();
    r->line = line;
    r->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!r->done || !PostMessageW(target, WM_CRONQ_IPC, reinterpret_cast<WPARAM>(r), 0)) {
        if (r->done) CloseHandle(r->done);
        delete r;
        return "ERROR\n";
    }

    std::string out;
    if (WaitForSingleObject(r->done, kUiTimeoutMs) == WAIT_OBJECT_0) {
        out = r->response;  // safe: the UI thread finished writing before it signalled
    } else {
        out = "BUSY\n";
    }
    Release(r);
    return out;
}

void HandleClient(HANDLE pipe) {
    std::string line;
    char buf[4096];
    while (line.size() < kMaxLine) {
        DWORD n = 0;
        if (!PipeIo(pipe, false, buf, sizeof(buf), &n, kClientTimeoutMs, g_stopEvent) || n == 0) break;
        line.append(buf, n);
        if (line.find('\n') != std::string::npos) break;
    }
    size_t nl = line.find('\n');
    if (nl == std::string::npos || nl >= kMaxLine) return;
    line.resize(nl);
    line = Trim(line);
    if (line.empty()) return;

    // Only the command word: arguments can be private (typed text, URLs). Status polling is not logged.
    const std::string verb = line.substr(0, line.find(' '));
    if (!EqNoCase(verb, "STATUS") && !EqNoCase(verb, "GET_GEOMETRY") && !EqNoCase(verb, "GET_OPACITY"))
        Log("IPC", "Received command: %.32s", verb.c_str());
    std::string resp = ExecuteOnUiThread(line);

    size_t off = 0;
    while (off < resp.size()) {
        DWORD n = 0;
        DWORD chunk = (DWORD)(resp.size() - off > 65536 ? 65536 : resp.size() - off);
        if (!PipeIo(pipe, true, &resp[off], chunk, &n, kClientTimeoutMs, nullptr) || n == 0)
            break;  // not abortable: KILL's reply must still be delivered
        off += n;
    }
    FlushFileBuffers(pipe);  // let the client read the reply before we disconnect
}

DWORD WINAPI ServerThread(LPVOID) {
    HANDLE connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!connectEvent) {
        CloseHandle(g_firstPipe);
        g_firstPipe = INVALID_HANDLE_VALUE;
        return 1;
    }
    HANDLE pipe = g_firstPipe;
    g_firstPipe = INVALID_HANDLE_VALUE;
    std::wstring name = PipeName();

    while (WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0) {
        if (pipe == INVALID_HANDLE_VALUE) {
            pipe =
                CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                 PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, Security().Get());
            if (pipe == INVALID_HANDLE_VALUE) {
                LogErr("IPC", "CreateNamedPipe failed (%lu); retrying.", GetLastError());
                if (WaitForSingleObject(g_stopEvent, 500) == WAIT_OBJECT_0) break;
                continue;
            }
        }

        OVERLAPPED ov;
        ZeroMemory(&ov, sizeof(ov));
        ov.hEvent = connectEvent;
        ResetEvent(connectEvent);

        bool connected = false;
        if (ConnectNamedPipe(pipe, &ov)) {
            connected = true;
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_PIPE_CONNECTED) {
                connected = true;
            } else if (err == ERROR_IO_PENDING) {
                HANDLE waits[2] = {connectEvent, g_stopEvent};
                DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (w == WAIT_OBJECT_0) {
                    DWORD ignored = 0;
                    connected = GetOverlappedResult(pipe, &ov, &ignored, FALSE) != FALSE;
                } else {
                    CancelIoEx(pipe, &ov);
                    DWORD ignored = 0;
                    GetOverlappedResult(pipe, &ov, &ignored, TRUE);
                }
            }
        }

        if (connected) HandleClient(pipe);
        DisconnectNamedPipe(pipe);  // the instance is reused for the next client
    }

    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
    CloseHandle(connectEvent);
    return 0;
}

}  // namespace

void Release(Request* r) {
    if (InterlockedDecrement(&r->refs) == 0) {
        if (r->done) CloseHandle(r->done);
        delete r;
    }
}

bool Send(const std::string& command, std::string* response) {
    const std::wstring name = PipeName();
    const std::string message = command + "\n";
    DWORD error = ERROR_FILE_NOT_FOUND;
    for (int attempt = 0; attempt < 12; ++attempt) {
        HANDLE pipe =
            CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            error = GetLastError();
            if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND &&
                error != ERROR_PIPE_NOT_CONNECTED && error != ERROR_NO_DATA)
                break;
            // A server reusing its pipe instance can briefly have no listening endpoint.
            // WaitNamedPipe can also return immediately during this gap; don't spin through retries.
            if (error == ERROR_PIPE_BUSY) WaitNamedPipeW(name.c_str(), 200);
            Sleep(10);
            continue;
        }
        DWORD written = 0;
        if (!WriteFile(pipe, message.data(), static_cast<DWORD>(message.size()), &written, nullptr) ||
            written != message.size()) {
            error = GetLastError();
            CloseHandle(pipe);
            // Retry only when the pipe disconnected before any command bytes were submitted.
            // Never replay a command after a successful or partial write.
            if (!written &&
                (error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_NO_DATA || error == ERROR_BROKEN_PIPE)) {
                Sleep(10);
                continue;
            }
            SetLastError(error);
            return false;
        }
        std::string out;
        char buffer[4096];
        DWORD count = 0;
        while (ReadFile(pipe, buffer, sizeof(buffer), &count, nullptr) && count) out.append(buffer, count);
        error = GetLastError();
        CloseHandle(pipe);
        if (out.empty()) {
            SetLastError(error);
            return false;
        }
        if (response) *response = std::move(out);
        return true;
    }
    if (error != ERROR_FILE_NOT_FOUND) LogErr("IPC", "Connecting to named pipe failed (%lu)", error);
    SetLastError(error);
    return false;
}

bool StartServer() {
    // FIRST_PIPE_INSTANCE makes creation fail if another cronq already owns the pipe.
    g_firstPipe = CreateNamedPipeW(
        PipeName().c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, Security().Get());
    if (g_firstPipe == INVALID_HANDLE_VALUE) {
        LogErr("MAIN", "Another instance of Cronq is already running (pipe error %lu).", GetLastError());
        return false;
    }

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stopEvent) g_thread = CreateThread(nullptr, 0, ServerThread, nullptr, 0, nullptr);
    if (!g_thread) {
        LogErr("MAIN", "Failed to start the IPC thread (%lu).", GetLastError());
        CloseHandle(g_firstPipe);
        g_firstPipe = INVALID_HANDLE_VALUE;
        if (g_stopEvent) {
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
        }
        return false;
    }
    Log("MAIN", "IPC pipe listening on '%s'.", Narrow(PipeName()).c_str());
    return true;
}

void SetTarget(HWND hwnd) { InterlockedExchangePointer(&g_target, hwnd); }

void StopServer() {
    SetTarget(nullptr);
    if (g_stopEvent) SetEvent(g_stopEvent);
    if (g_thread) {
        // FlushFileBuffers may be blocked by an unresponsive client; cancel that synchronous wait.
        CancelSynchronousIo(g_thread);
        if (WaitForSingleObject(g_thread, kUiTimeoutMs + 2500) != WAIT_OBJECT_0) {
            LogErr("IPC", "Server did not stop; retaining synchronization objects until process exit");
            return;
        }
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
}

}  // namespace ipc
}  // namespace cronq
