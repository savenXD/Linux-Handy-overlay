#include "app.h"

#include "config.h"
#include "browser.h"
#include "ghost.h"
#include "hotkey.h"
#include "input.h"
#include "ipc.h"
#include "jsonlite.h"
#include "overlay.h"
#include "snap.h"
#include "ui_bridge.h"
#include "theme.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cronq {
namespace app {

namespace {

std::string g_testEntry;

bool StartsWith(const std::string& s, const char* prefix) {
    return s.compare(0, strlen(prefix), prefix) == 0;
}

}  // namespace

const std::string& TestEntryText() { return g_testEntry; }

void SetTestEntryText(const std::string& text, bool notifyUi) {
    g_testEntry = text;
    if (notifyUi) {
        ui::PostToUi("{\"type\":\"set-text\",\"value\":\"" + json::Escape(g_testEntry) + "\"}");
    }
}

std::string HandleCommand(const std::string& rawLine) {
    const std::string line = Trim(rawLine);
    std::string resp = "OK\n";

    if (EqNoCase(line, "TOGGLE")) {
        overlay::Toggle();
    } else if (EqNoCase(line, "SHOW")) {
        overlay::Show();
    } else if (EqNoCase(line, "HIDE")) {
        overlay::Hide();
    } else if (EqNoCase(line, "KILL") || EqNoCase(line, "QUIT") || EqNoCase(line, "EXIT")) {
        Log("MAIN", "Termination requested via IPC.");
        PostMessageW(overlay::Hwnd(), WM_CRONQ_QUIT, 0, 0);  // reply first, quit after the reply is queued
    } else if (EqNoCase(line, "MODE mirror")) {
        input::SetKeyMode(KeyMode::Mirror);
    } else if (EqNoCase(line, "MODE exclusive")) {
        input::SetKeyMode(KeyMode::Exclusive);
    } else if (EqNoCase(line, "MODE")) {
        input::CycleKeyMode();
    } else if (EqNoCase(line, "GHOST on")) {
        input::SetGhostMode(input::GhostMode::On);
    } else if (EqNoCase(line, "GHOST off")) {
        input::SetGhostMode(input::GhostMode::Off);
    } else if (EqNoCase(line, "GHOST")) {
        input::ToggleGhostMode();
    } else if (StartsWith(line, "LOAD ")) {
        overlay::SwitchApp("browser");
        browser::LoadInBrowser(line.substr(5));
    } else if (StartsWith(line, "APP ")) {
        overlay::SwitchApp(Trim(line.substr(4)));
    } else if (EqNoCase(line, "BACK")) {
        overlay::Back();
    } else if (EqNoCase(line, "HOME")) {
        overlay::SwitchApp("home");
    } else if (EqNoCase(line, "RELOAD")) {
        browser::Reload();
    } else if (EqNoCase(line, "UNIVERSAL_RESET") || EqNoCase(line, "RESET")) {
        overlay::UniversalReset();
    } else if (EqNoCase(line, "GET_TEST_ENTRY")) {
        resp = g_testEntry + "\n";
    } else if (EqNoCase(line, "CLEAR_TEST_ENTRY")) {
        SetTestEntryText("", true);
    } else if (StartsWith(line, "SET_TEST_ENTRY")) {
        std::string val = (line.size() > 14 && line[14] == ' ') ? line.substr(15) : line.substr(14);
        SetTestEntryText(val, true);
    } else if (EqNoCase(line, "HAS_MODAL")) {
        resp = browser::HasPopup() ? "TRUE\n" : "FALSE\n";
    } else if (EqNoCase(line, "CLOSE_MODAL")) {
        browser::ClosePopup();
    } else if (EqNoCase(line, "SNAP")) {
        snap::Run();
    } else if (EqNoCase(line, "GHOST_WRITE")) {
        ghost::Run();
    } else if (EqNoCase(line, "PASTE")) {
        browser::PasteImageOrText();
    } else if (StartsWith(line, "SET_OPACITY ")) {
        overlay::SetOpacity(strtod(line.c_str() + 12, nullptr));
    } else if (EqNoCase(line, "GET_OPACITY")) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f\n", overlay::GetOpacity());
        resp = buf;
    } else if (StartsWith(line, "SET_SIZE ")) {
        int w = 0, h = 0;
        if (sscanf(line.c_str() + 9, "%d %d", &w, &h) == 2) overlay::SetSize(w, h);
    } else if (EqNoCase(line, "FULLSCREEN")) {
        overlay::SetFullscreen();
    } else if (EqNoCase(line, "DEFAULT_SIZE")) {
        overlay::SetDefaultSize();
    } else if (EqNoCase(line, "GET_GEOMETRY")) {
        int x, y, w, h;
        overlay::GetGeometry(&x, &y, &w, &h);
        char buf[96];
        snprintf(buf, sizeof(buf), "X=%d Y=%d W=%d H=%d\n", x, y, w, h);
        resp = buf;
    } else if (EqNoCase(line, "RELOAD_CONFIG")) {
        config::Reload();
        overlay::ApplyConfig();
        hotkey::ReloadKeys();
        theme::Reload();
    } else if (EqNoCase(line, "STATUS")) {
        char buf[256];
        snprintf(buf, sizeof(buf), "MODE=%s TARGET=%s GHOST=%s GRAB=%s VISIBLE=%s APP=%s LOCKED=%s APPEARANCE=%d\n",
                 input::GetKeyMode() == KeyMode::Exclusive ? "EXCLUSIVE" : "MIRROR",
                 input::GetTypingTarget() == input::TypingTarget::Overlay ? "OVERLAY" : "NONE",
                 input::GetGhostMode() == input::GhostMode::On ? "GHOST" : "SOLID",
                 input::GetGrabStatus() == input::GrabStatus::Active
                     ? "ACTIVE"
                     : (input::GetGrabStatus() == input::GrabStatus::Failed ? "FAILED" : "NONE"),
                 overlay::IsVisible() ? "TRUE" : "FALSE", overlay::CurrentApp(),
                 hotkey::IsLocked() ? "TRUE" : "FALSE", theme::Mode());
        resp = buf;
    } else {
        resp = "UNKNOWN_COMMAND\n";
    }
    return resp;
}

LRESULT OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CRONQ_IPC: {
            ipc::Request* r = reinterpret_cast<ipc::Request*>(wParam);
            r->response = HandleCommand(r->line);
            SetEvent(r->done);
            ipc::Release(r);
            return 0;
        }
        case WM_CRONQ_HOTKEY:
            hotkey::Dispatch((hotkey::Id)wParam);
            return 0;
        case WM_CRONQ_ALTFIX:
            hotkey::InjectAltSuppressor();
            return 0;
        case WM_CRONQ_CLICK:
            if (wParam) browser::NotifyPointerClick();
            input::OnMouseClick(wParam != 0);
            return 0;
        case WM_CRONQ_KEY:
            input::OnKeyActivity();
            if (input::GetTypingTarget() == input::TypingTarget::Overlay || !(lParam & 1))
                browser::DispatchKeyEvent(wParam, lParam);
            return 0;
        case WM_CRONQ_CLIPBOARD:
            input::OnKeyActivity();
            if (input::GetTypingTarget() == input::TypingTarget::Overlay || !(lParam & 1))
                browser::DispatchClipboardAction(wParam, lParam);
            return 0;
        case WM_CRONQ_QUIT:
            Log("MAIN", "Shutdown requested.");
            PostQuitMessage(0);
            return 0;
        default:
            return 0;
    }
}

}  // namespace app
}  // namespace cronq
