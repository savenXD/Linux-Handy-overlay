#pragma once

namespace cronq {
namespace crash_handler {

// Installs the unhandled exception filter for the daemon process.
// On crash, writes crash-<timestamp>-<pid>.log and a small .dmp (newest 3 kept) to DataDir(),
// then returns EXCEPTION_EXECUTE_HANDLER.
void Install();

}  // namespace crash_handler
}  // namespace cronq
