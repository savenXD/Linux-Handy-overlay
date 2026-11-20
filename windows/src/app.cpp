#include "app.h"

#include "config.h"
#include "chat_view.h"
#include "hotkey.h"
#include "input.h"
#include "ipc.h"
#include "overlay.h"
#include "snap.h"
#include "theme.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cronq {
namespace app {

namespace {

bool StartsWith(const std::string& s, const char* prefix) {
    return s.compare(0, strlen(prefix), prefix) == 0;
}

}  // namespace

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
    } else if (EqNoCase(line, "MODE normal")) {
        input::ReleaseGrab();
    } else if (EqNoCase(line, "MODE overlay")) {
        input::SetTypingTarget(input::TypingTarget::Overlay);
    } else if (EqNoCase(line, "MODE")) {
        input::ToggleTypingTarget();
    } else if (EqNoCase(line, "GHOST on")) {
        input::SetGhostMode(input::GhostMode::On);
    } else if (EqNoCase(line, "GHOST off")) {
        input::SetGhostMode(input::GhostMode::Off);
    } else if (EqNoCase(line, "GHOST")) {
        input::ToggleGhostMode();
    } else if (StartsWith(line, "APP ")) {
        const auto name = Trim(line.substr(4));
        if (EqNoCase(name, "home") || EqNoCase(name, "chatgpt")) overlay::SwitchApp(name);
        else resp = "UNKNOWN_APP\n";
    } else if (EqNoCase(line, "BACK")) {
        overlay::Back();
    } else if (EqNoCase(line, "HOME")) {
        overlay::SwitchApp("home");
    } else if (EqNoCase(line, "RELOAD")) {
        chat_view::Reload();
    } else if (EqNoCase(line, "UNIVERSAL_RESET") || EqNoCase(line, "RESET")) {
        overlay::UniversalReset();
    } else if (EqNoCase(line, "HAS_MODAL")) {
        resp = chat_view::HasPopup() ? "TRUE\n" : "FALSE\n";
    } else if (EqNoCase(line, "CLOSE_MODAL")) {
        chat_view::ClosePopup();
    } else if (EqNoCase(line, "SNAP")) {
        snap::Run();
    } else if (EqNoCase(line, "SNAP_WGC")) {
        snap::RunExperimental();
    } else if (EqNoCase(line, "SNAP_WGC_WINDOW")) {
        snap::RunExperimentalWindow();
    } else if (EqNoCase(line, "PASTE")) {
        chat_view::PasteImageOrText();
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
                 input::GetTypingTarget() == input::TypingTarget::Overlay ? "OVERLAY" : "NORMAL",
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
        case WM_CRONQ_INPUT_STATE:
            if (!hotkey::IsHookInstalled()) input::ReleaseGrab();
            overlay::QueueDraw();
            return 0;
        case WM_CRONQ_IPC: {
            ipc::Request* r = reinterpret_cast<ipc::Request*>(wParam);
            r->response = HandleCommand(r->line);
            SetEvent(r->done);
            ipc::Release(r);
            return 0;
        }
        case WM_CRONQ_HOTKEY:
            hotkey::Dispatch((hotkey::Id)wParam, reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_CRONQ_ALTFIX:
            hotkey::InjectAltSuppressor();
            return 0;
        case WM_CRONQ_CLICK: {
            bool inside = wParam != 0;
            if (inside) {
                // Same hit test Windows uses for the click itself: colour-keyed (camouflage) pixels,
                // click-through mode and windows stacked above the overlay all resolve elsewhere.
                POINT pt = {static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
                HWND hit = WindowFromPoint(pt);
                inside = hit && GetAncestor(hit, GA_ROOT) == overlay::Hwnd();
            }
            input::OnMouseClick(inside);
            return 0;
        }
        case WM_CRONQ_KEY:
            if (input::GetTypingTarget() == input::TypingTarget::Overlay || !(lParam & 1))
                chat_view::DispatchKeyEvent(wParam, lParam);
            return 0;
        case WM_CRONQ_CLIPBOARD:
            if (input::GetTypingTarget() == input::TypingTarget::Overlay || !(lParam & 1))
                chat_view::DispatchClipboardAction(wParam, lParam);
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
