// Application-level glue: IPC command dispatch and hotkey actions (UI thread only).
#pragma once

#include "common.h"

namespace cronq {
namespace app {

// Executes one IPC command line and returns the reply text (always newline terminated).
std::string HandleCommand(const std::string& line);

// Handles WM_CRONQ_* messages forwarded by the overlay window procedure.
LRESULT OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam);

// Test entry text used by the GET/SET/CLEAR_TEST_ENTRY commands.
const std::string& TestEntryText();
void SetTestEntryText(const std::string& text, bool notifyUi = true);

}  // namespace app
}  // namespace cronq
