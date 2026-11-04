#include "ui_bridge.h"

#include <deque>
#include <locale>
#include <sstream>
#include <utility>

#include "browser.h"
#include "app.h"
#include "ghost.h"
#include "input.h"
#include "jsonlite.h"
#include "overlay.h"
#include "snap.h"
#include "theme.h"
namespace cronq {
namespace ui {
namespace {
bool g_ready = false;
std::deque<std::string> g_queue;
bool Number(const std::map<std::string, std::string>& m, const char* key, int* value) {
    auto it = m.find(key);
    return it != m.end() && json::ParseInt(it->second, 0, 100000, value);
}
}  // namespace
bool IsReady() { return g_ready; }
void Reset() {
    g_ready = false;
    g_queue.clear();
}
void PostToUi(const std::string& msg) {
    if (!browser::IsAvailable()) return;
    if (g_ready)
        browser::PostUiMessage(msg);
    else {
        // DECISION: bounded pre-ready queue; state itself is recomputed on ready.
        if (g_queue.size() == 32) g_queue.pop_front();
        g_queue.push_back(msg);
    }
}
void PushState() {
    if (!browser::IsAvailable()) return;
    int x, y, w, h;
    overlay::GetGeometry(&x, &y, &w, &h);
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << "{\"type\":\"state\",\"mode\":\""
      << (input::GetKeyMode() == KeyMode::Exclusive ? "exclusive" : "mirror")
      << "\",\"ghost\":" << (input::GetGhostMode() == input::GhostMode::On ? "true" : "false")
      << ",\"grab\":\""
      << (input::GetGrabStatus() == input::GrabStatus::Failed   ? "failed"
          : input::GetGrabStatus() == input::GrabStatus::Active ? "active"
                                                                : "none")
      << "\",\"app\":\"" << json::Escape(overlay::CurrentApp()) << "\",\"opacity\":" << overlay::GetOpacity()
      << ",\"width\":" << w << ",\"height\":" << h
      << ",\"appearance\":" << theme::Mode() << ",\"palette\":" << theme::PaletteJson()
      << ",\"canBack\":" << (browser::CanGoBack() ? "true" : "false")
      << ",\"loading\":" << (browser::IsLoading() ? "true" : "false")
      << ",\"progress\":" << browser::Progress() << ",\"url\":\"" << json::Escape(browser::CurrentUri())
      << "\",\"popup\":" << (browser::HasPopup() ? "true" : "false") << ",\"scale\":" << overlay::Scale()
      << "}";
    const auto state = s.str();
    if (!g_ready) return;
    PostToUi(state);
}
void OnMessage(const std::string& text) {
    std::map<std::string, std::string> m;
    if (!json::ParseFlat(text, &m)) {
        LogErr("UI", "Rejected malformed message");
        return;
    }
    const auto& type = m["type"];
    if (type == "ready") {
        g_ready = true;
        auto queued = std::move(g_queue);
        g_queue.clear();
        for (const auto& msg : queued) PostToUi(msg);
        PushState();
        const auto timing = ghost::GetTiming();
        PostToUi("{\"type\":\"ghost-settings\",\"human\":" + std::string(timing.human ? "true" : "false") +
                 ",\"safe\":" + (timing.safe ? "true" : "false") +
                 ",\"speed\":" + std::to_string(timing.speed) + "}");
        app::SetTestEntryText(app::TestEntryText(), true);
        return;
    }
    if (!g_ready) return;
    if (type == "appearance") {
        int mode;
        if (Number(m,"mode",&mode) && mode <= 3) {
            if (!theme::SetMode(mode)) overlay::ShowToast("Could not save appearance");
            PushState();
        }
    } else if (type == "slot") {
        // Ignore a late rectangle from the page that was just switched away from.
        if (m["app"] != overlay::CurrentApp() || (m["popup"] == "true") != browser::HasPopup()) return;
        int x, y, w, h;
        if (!Number(m, "x", &x) || !Number(m, "y", &y) || !Number(m, "w", &w) || !Number(m, "h", &h)) return;
        RECT r = {x, y, x + w, y + h};
        browser::ResizeContent(r);
    } else if (type == "nav") {
        const auto& app = m["app"];
        if (app == "home" || app == "browser" || app == "chatgpt" || app == "ghost") overlay::SwitchApp(app);
    } else if (type == "back")
        overlay::Back();
    else if (type == "reset")
        overlay::UniversalReset();
    else if (type == "snap")
        snap::Run();
    else if (type == "ghost-start")
        ghost::OnGhostStartRequested();
    else if (type == "ghost-text") {
        auto it = m.find("value");
        if (it != m.end()) {
            ghost::OnGhostText(it->second);
        }
    } else if (type == "ghost-settings") {
        bool human = (m.find("human") != m.end() && m.at("human") == "true");
        int speed = 0;
        Number(m, "speed", &speed);
        bool safe = (m.find("safe") != m.end() && m.at("safe") == "true");
        ghost::OnGhostSettings(human, speed, safe);
    } else if (type == "focus") {
        auto it = m.find("where");
        browser::SetUrlInputFocused(it != m.end() && it->second == "url");
    } else if (type == "reload")
        browser::Reload();
    else if (type == "stop")
        browser::Stop();
    else if (type == "url") {
        overlay::SwitchApp("browser");
        browser::LoadInBrowser(m["value"]);
    } else if (type == "search-home")
        overlay::SwitchApp("browser");
    else if (type == "window") {
        if (m["action"] == "hide")
            overlay::Hide();
        else if (m["action"] == "fullscreen")
            overlay::SetFullscreen();
    } else if (type == "pill") {
        if (m["which"] == "mode")
            input::CycleKeyMode();
        else if (m["which"] == "ghost")
            input::ToggleGhostMode();
    } else if (type == "drag") {
        int edges = 0;
        if (m["kind"] == "move")
            overlay::BeginExternalDrag(true, 0);
        else if (m["kind"] == "resize" && Number(m, "edges", &edges) && edges > 0 && edges <= 15)
            overlay::BeginExternalDrag(false, edges);
    } else if (type == "dblclick-handle")
        overlay::ToggleHeight();
    else if (type == "popup-close")
        browser::ClosePopup();
    else
        Log("UI", "Unknown message type '%s'", type.c_str());
}
}  // namespace ui
}  // namespace cronq
