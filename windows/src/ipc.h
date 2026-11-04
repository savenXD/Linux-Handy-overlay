// Single-instance command channel between `cronq` (CLI) and the running overlay.
// Linux uses a Unix socket; Windows uses a per-user named pipe with the same line protocol:
// the client sends "COMMAND\n", the server answers with text and closes the connection.
#pragma once

#include "common.h"

namespace cronq {
namespace ipc {

// A command waiting for the UI thread. Reference counted: the pipe thread and the UI thread each hold one
// reference, so a UI thread that answers late never touches freed memory.
struct Request {
    std::string line;
    std::string response;
    HANDLE done = nullptr;
    volatile LONG refs = 2;
};
void Release(Request* r);

// ---- client side
bool Send(const std::string& command, std::string* response);
// ---- server side
// Returns false if another instance already owns the pipe.
bool StartServer();
// Commands received before this is called are answered with "STARTING".
void SetTarget(HWND hwnd);
void StopServer();

}  // namespace ipc
}  // namespace cronq
