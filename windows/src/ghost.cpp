#include "ghost.h"

#include <shlwapi.h>
#include <windows.h>
#include <fstream>
#include <sstream>
#include <vector>

#include "app.h"
#include "config.h"
#include "ghost_logic.h"
#include "hotkey.h"
#include "ini_writer.h"
#include "input.h"
#include "jsonlite.h"
#include "overlay.h"
#include "ui_bridge.h"

namespace cronq {
namespace ghost {

namespace {

bool g_isTyping = false;
bool g_sendFailed = false;
bool g_guardActive = false;
bool g_timingLoaded = false;
ghost_logic::Timing g_timing;
int64_t g_lastRunUs = 0;

int g_countdownSeconds = 0;
bool g_countdownActive = false;

// Typing run context
struct TypingRun {
    HWND targetWindow = nullptr;
    std::string appName;
    bool capsWasOn = false;
    int settleRetries = 0;
    std::unique_ptr<ghost_logic::Typist> typist;
    int phase = 0;  // 0: CHAR, 1: PRESS, 2: RELEASE
    ghost_logic::PlanItem currentItem;
    ghost_logic::KeyInterval currentInterval;

    // Resolved key
    WORD resolvedVk = 0;
    WORD resolvedScan = 0;
    bool isUnicode = false;
    bool keyHeld = false;
    bool needShift = false;
    bool needCtrl = false;
    bool needAlt = false;

    int64_t startUs = 0;
    int64_t lastProgressUs = 0;
    int64_t lastEtaUs = 0;
};

std::unique_ptr<TypingRun> g_run;

bool HasUIAccess() {
    HANDLE token = nullptr;
    DWORD enabled = 0, size = sizeof(enabled);
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    BOOL ok = GetTokenInformation(token, TokenUIAccess, &enabled, sizeof(enabled), &size);
    CloseHandle(token);
    return ok && enabled;
}

bool IsProcessElevated(HANDLE hProcess) {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    BOOL ok = GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(hToken);
    return ok && elevation.TokenIsElevated != 0;
}

std::string GetProcessBaseName(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return "app";

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return "app";

    wchar_t exePath[MAX_PATH];
    DWORD len = MAX_PATH;
    std::string name = "app";
    if (QueryFullProcessImageNameW(hProc, 0, exePath, &len) && len > 0) {
        std::wstring full(exePath, len);
        size_t slash = full.find_last_of(L"\\/");
        std::wstring base = (slash != std::wstring::npos) ? full.substr(slash + 1) : full;
        if (base.size() > 4 && base.compare(base.size() - 4, 4, L".exe") == 0) {
            base = base.substr(0, base.size() - 4);
        }
        name = Narrow(base);
    }
    CloseHandle(hProc);
    return name;
}

bool SendTaggedKey(WORD vk, WORD scan, DWORD flags) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = scan;
    in.ki.dwFlags = flags;
    in.ki.dwExtraInfo = kInjectTag;
    if (SendInput(1, &in, sizeof(INPUT)) != 1) {
        g_sendFailed = true;
        LogErr("GHOST", "SendInput failed (%lu)", GetLastError());
        return false;
    }
    return true;
}

void SendUnicodeChar(uint32_t cp, bool keyUp) {
    DWORD flags = KEYEVENTF_UNICODE;
    if (keyUp) flags |= KEYEVENTF_KEYUP;

    if (cp <= 0xFFFF) {
        SendTaggedKey(0, static_cast<WORD>(cp), flags);
    } else {
        // UTF-16 surrogate pair
        uint32_t v = cp - 0x10000;
        WORD high = static_cast<WORD>(0xD800 + (v >> 10));
        WORD low = static_cast<WORD>(0xDC00 + (v & 0x3FF));
        SendTaggedKey(0, high, flags);
        SendTaggedKey(0, low, flags);
    }
}

void ReleaseHeldKey() {
    if (!g_run || !g_run->keyHeld) return;
    if (g_run->isUnicode)
        SendUnicodeChar(g_run->currentItem.cp, true);
    else
        SendTaggedKey(g_run->resolvedVk, g_run->resolvedScan, KEYEVENTF_KEYUP);
    g_run->keyHeld = false;
}

void ReleaseActiveModifiers() {
    if (!g_run) return;
    if (g_run->needShift) SendTaggedKey(VK_SHIFT, 0, KEYEVENTF_KEYUP);
    if (g_run->needCtrl) SendTaggedKey(VK_CONTROL, 0, KEYEVENTF_KEYUP);
    if (g_run->needAlt) SendTaggedKey(VK_MENU, 0, KEYEVENTF_KEYUP);
    g_run->needShift = false;
    g_run->needCtrl = false;
    g_run->needAlt = false;
}

void UpdateGhostUi(const std::string& statusText, const std::string& etaText, const std::string& buttonText,
                   const std::string& state) {
    std::string json = "{\"type\":\"ghost-status\",\"text\":\"" + json::Escape(statusText) + "\",\"eta\":\"" +
                       json::Escape(etaText) + "\",\"button\":\"" + json::Escape(buttonText) +
                       "\",\"state\":\"" + json::Escape(state) + "\"}";
    ui::PostToUi(json);
}

}  // namespace

bool Init() {
    g_isTyping = false;
    g_countdownActive = false;
    g_timingLoaded = false;
    return true;
}

void Cleanup() {
    Cancel();
    g_guardActive = false;
    hotkey::SetTypingGuard(false);
    if (overlay::Hwnd()) {
        KillTimer(overlay::Hwnd(), kGhostCountdownTimerId);
        KillTimer(overlay::Hwnd(), kGhostSettleTimerId);
        KillTimer(overlay::Hwnd(), kGhostStepTimerId);
        KillTimer(overlay::Hwnd(), kGhostGuardDisengageTimerId);
    }
}

bool IsTyping() { return g_isTyping; }

void OnForegroundChanged(HWND hwnd) {
    if (g_isTyping && g_run && hwnd != g_run->targetWindow) {
        Cancel();
        overlay::ShowToast(ghost_logic::kToastFocusChanged);
    }
}

void OnProgressTick() {
    if (!g_isTyping || !g_run || !g_run->startUs) return;
    const auto elapsed = (NowUs() - g_run->startUs) / 1000;
    UpdateGhostUi(ghost_logic::FormatStatus(g_run->typist->TypedChars(), g_run->typist->TotalChars()),
                  ghost_logic::FormatEta(g_run->typist->EtaRemainingMs(elapsed)), "Cancel", "typing");
}

bool IsGuardActive() { return g_guardActive; }

ghost_logic::Timing GetTiming() {
    if (!g_timingLoaded) {
        g_timing = ghost_logic::DefaultTiming();
        std::wstring path = ConfigDir() + L"\\config.ini";
        std::ifstream f(path.c_str(), std::ios::binary);
        if (f.is_open()) {
            std::stringstream ss;
            ss << f.rdbuf();
            std::string content = ss.str();
            g_timing.human = (ini_writer::GetIniInt(content, "GhostTiming", "human", 1) != 0);
            g_timing.speed = ini_writer::GetIniInt(content, "GhostTiming", "speed", 0);
            g_timing.safe = (ini_writer::GetIniInt(content, "GhostTiming", "safe", 0) != 0);
        }
        g_timing = ghost_logic::ValidateTiming(g_timing);
        g_timingLoaded = true;
    }
    return g_timing;
}

void SetTiming(ghost_logic::Timing t) {
    g_timing = ghost_logic::ValidateTiming(t);
    g_timingLoaded = true;

    std::wstring path = ConfigDir() + L"\\config.ini";
    std::string content;
    {
        std::ifstream f(path.c_str(), std::ios::binary);
        if (f.is_open()) {
            std::stringstream ss;
            ss << f.rdbuf();
            content = ss.str();
        }
    }
    ini_writer::SetIniInt(content, "GhostTiming", "human", g_timing.human ? 1 : 0);
    ini_writer::SetIniInt(content, "GhostTiming", "speed", g_timing.speed);
    ini_writer::SetIniInt(content, "GhostTiming", "safe", g_timing.safe ? 1 : 0);

    const auto temporary = path + L".tmp";
    std::ofstream out(temporary.c_str(), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        overlay::ShowToast(ghost_logic::kToastSettingsSaveFailed);
    } else {
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.close();
        if (!out || !MoveFileExW(temporary.c_str(), path.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str());
            overlay::ShowToast(ghost_logic::kToastSettingsSaveFailed);
        }
    }
}

void OnGhostText(const std::string& text) { app::SetTestEntryText(text, false); }

void OnGhostSettings(bool human, int speed, bool safe) {
    ghost_logic::Timing t;
    t.human = human;
    t.speed = speed;
    t.safe = safe;
    SetTiming(t);
}

void OnCountdownTick() {
    if (!g_countdownActive) return;
    g_countdownSeconds--;
    if (g_countdownSeconds > 0) {
        UpdateGhostUi(ghost_logic::FormatCountdown(g_countdownSeconds), "", "Cancel", "typing");
    } else {
        KillTimer(overlay::Hwnd(), kGhostCountdownTimerId);
        g_countdownActive = false;
        Run();
    }
}

void OnGhostStartRequested() {
    if (g_countdownActive) {
        KillTimer(overlay::Hwnd(), kGhostCountdownTimerId);
        g_countdownActive = false;
        UpdateGhostUi("Ready", "", "Start", "idle");
        return;
    }
    if (g_isTyping) {
        Cancel();
        return;
    }

    const std::string& rawText = app::TestEntryText();
    if (rawText.empty()) {
        overlay::ShowToast(ghost_logic::kToastNothingToType);
        UpdateGhostUi("Ready", "", "Start", "failed");
        return;
    }

    // Save settings before starting
    SetTiming(GetTiming());

    input::ReleaseGrab();
    hotkey::SetClipboardShield(false);
    input::SetTypingTarget(input::TypingTarget::None);

    g_countdownSeconds = 3;
    g_countdownActive = true;
    UpdateGhostUi(ghost_logic::FormatCountdown(3), "", "Cancel", "typing");
    if (!SetTimer(overlay::Hwnd(), kGhostCountdownTimerId, 1000, nullptr)) Cancel();
}

void Cancel() {
    if (g_countdownActive) {
        KillTimer(overlay::Hwnd(), kGhostCountdownTimerId);
        g_countdownActive = false;
    }
    if (!g_isTyping) {
        UpdateGhostUi("Ready", "", "Start", "idle");
        return;
    }

    KillTimer(overlay::Hwnd(), kGhostSettleTimerId);
    KillTimer(overlay::Hwnd(), kGhostStepTimerId);
    KillTimer(overlay::Hwnd(), kGhostProgressTimerId);

    if (g_run) {
        ReleaseHeldKey();
        ReleaseActiveModifiers();
        if (g_run->capsWasOn) {
            SendTaggedKey(VK_CAPITAL, 0, 0);
            SendTaggedKey(VK_CAPITAL, 0, KEYEVENTF_KEYUP);
        }
        g_run.reset();
    }

    // Disengage guard after 25ms
    if (!SetTimer(overlay::Hwnd(), kGhostGuardDisengageTimerId, 25, nullptr)) {
        g_guardActive = false;
        hotkey::SetTypingGuard(false);
    }
    g_isTyping = false;

    UpdateGhostUi("Ready", "", "Start", "failed");
    overlay::ShowToast(ghost_logic::kToastTypingCancelled);
    Log("GHOST", "Cancelling active Ghost Writer typing.");
}

void OnGuardDisengageTimeout() {
    KillTimer(overlay::Hwnd(), kGhostGuardDisengageTimerId);
    if (g_isTyping) return;
    g_guardActive = false;
    hotkey::SetTypingGuard(false);
    Log("GHOST", "Self-echo guard disengaged (+25ms settling complete).");
}

void FinishTyping() {
    KillTimer(overlay::Hwnd(), kGhostStepTimerId);
    KillTimer(overlay::Hwnd(), kGhostProgressTimerId);
    g_isTyping = false;

    size_t typed = g_run ? g_run->typist->TypedChars() : 0;
    std::string appName = g_run ? g_run->appName : "app";

    if (g_run) {
        ReleaseHeldKey();
        ReleaseActiveModifiers();
        if (g_run->capsWasOn) {
            SendTaggedKey(VK_CAPITAL, 0, 0);
            SendTaggedKey(VK_CAPITAL, 0, KEYEVENTF_KEYUP);
        }
        g_run.reset();
    }

    if (!SetTimer(overlay::Hwnd(), kGhostGuardDisengageTimerId, 25, nullptr)) {
        g_guardActive = false;
        hotkey::SetTypingGuard(false);
    }

    char toastBuf[128];
    if (!appName.empty() && appName != "app") {
        snprintf(toastBuf, sizeof(toastBuf), "Typed %zu chars in %s", typed, appName.c_str());
    } else {
        snprintf(toastBuf, sizeof(toastBuf), "Typed %zu chars", typed);
    }
    overlay::ShowToast(toastBuf);

    UpdateGhostUi(ghost_logic::FormatStatus(typed, typed), "", "Start", "done");
    Log("GHOST", "Finished typing %zu characters.", typed);
}

void StepTyping() {
    if (!g_isTyping || !g_run) return;
    KillTimer(overlay::Hwnd(), kGhostStepTimerId);

    if (g_sendFailed) {
        Cancel();
        overlay::ShowToast(ghost_logic::kToastUnsupportedChar);
        return;
    }

    // Verify foreground focus has not changed
    HWND currentFocus = GetForegroundWindow();
    if (currentFocus != g_run->targetWindow) {
        Cancel();
        overlay::ShowToast(ghost_logic::kToastFocusChanged);
        return;
    }

    HKL hkl = GetKeyboardLayout(GetWindowThreadProcessId(g_run->targetWindow, nullptr));

    // src/ghost.c instant_step: batches of 16 without timer dwell between every key.
    if (g_run->phase == 0 && !g_run->typist->GetTiming().human) {
        for (int n = 0; n < 16 && g_run && g_run->typist->HasNext(); ++n) {
            auto item = g_run->typist->Next();
            g_run->currentItem = item;
            if (item.cp == '\n' || item.cp == '\t' || item.cp == '\b') {
                WORD vk = item.cp == '\n' ? VK_RETURN : item.cp == '\t' ? VK_TAB : VK_BACK;
                SendTaggedKey(vk, 0, 0);
                SendTaggedKey(vk, 0, KEYEVENTF_KEYUP);
            } else {
                SendUnicodeChar(item.cp, false);
                SendUnicodeChar(item.cp, true);
            }
            if (g_sendFailed) {
                Cancel();
                overlay::ShowToast(ghost_logic::kToastUnsupportedChar);
                return;
            }
            g_run->typist->OnCharTyped(item.cp, item.count);
        }
        if (g_run && !g_run->typist->HasNext())
            FinishTyping();
        else if (!SetTimer(overlay::Hwnd(), kGhostStepTimerId, 2, nullptr))
            Cancel();
        return;
    }

    if (g_run->phase == 0) {  // CHAR phase
        if (!g_run->typist->HasNext()) {
            FinishTyping();
            return;
        }

        g_run->currentItem = g_run->typist->Next();
        uint32_t cp = g_run->currentItem.cp;

        g_run->resolvedVk = 0;
        g_run->resolvedScan = 0;
        g_run->isUnicode = false;
        g_run->needShift = false;
        g_run->needCtrl = false;
        g_run->needAlt = false;

        if (cp == '\n') {
            g_run->resolvedVk = VK_RETURN;
            g_run->resolvedScan = static_cast<WORD>(MapVirtualKeyExW(VK_RETURN, MAPVK_VK_TO_VSC, hkl));
        } else if (cp == '\t') {
            g_run->resolvedVk = VK_TAB;
            g_run->resolvedScan = static_cast<WORD>(MapVirtualKeyExW(VK_TAB, MAPVK_VK_TO_VSC, hkl));
        } else if (cp == '\b') {
            g_run->resolvedVk = VK_BACK;
            g_run->resolvedScan = static_cast<WORD>(MapVirtualKeyExW(VK_BACK, MAPVK_VK_TO_VSC, hkl));
        } else {
            SHORT scanRes = (cp <= 0xFFFF) ? VkKeyScanExW(static_cast<WCHAR>(cp), hkl) : -1;
            if (scanRes != -1) {
                g_run->resolvedVk = static_cast<WORD>(scanRes & 0xFF);
                g_run->resolvedScan =
                    static_cast<WORD>(MapVirtualKeyExW(g_run->resolvedVk, MAPVK_VK_TO_VSC, hkl));
                int modFlags = (scanRes >> 8) & 0xFF;
                g_run->needShift = (modFlags & 1) != 0;
                g_run->needCtrl = (modFlags & 2) != 0;
                g_run->needAlt = (modFlags & 4) != 0;
            } else {
                g_run->isUnicode = true;
            }
        }

        bool needsMod = g_run->needShift || g_run->needCtrl || g_run->needAlt;
        g_run->currentInterval = g_run->typist->ComputeInterval(cp, needsMod, g_run->isUnicode);

        if (needsMod) {
            if (g_run->needShift) SendTaggedKey(VK_SHIFT, 0, 0);
            if (g_run->needCtrl) SendTaggedKey(VK_CONTROL, 0, 0);
            if (g_run->needAlt) SendTaggedKey(VK_MENU, 0, 0);
        }

        g_run->phase = 1;  // Proceed to PRESS
        int nextMs = std::max(1, g_run->currentInterval.leadMs);
        if (!SetTimer(overlay::Hwnd(), kGhostStepTimerId, nextMs, nullptr)) Cancel();

    } else if (g_run->phase == 1) {  // PRESS phase
        g_run->keyHeld = true;
        if (g_run->isUnicode) {
            SendUnicodeChar(g_run->currentItem.cp, false);
        } else {
            DWORD flags = 0;
            if (g_run->resolvedVk == VK_RETURN || g_run->resolvedVk == VK_TAB ||
                g_run->resolvedVk == VK_BACK) {
                // normal
            }
            SendTaggedKey(g_run->resolvedVk, g_run->resolvedScan, flags);
        }

        g_run->phase = 2;  // Proceed to RELEASE
        int nextMs = std::max(1, g_run->currentInterval.dwellMs);
        if (!SetTimer(overlay::Hwnd(), kGhostStepTimerId, nextMs, nullptr)) Cancel();

    } else if (g_run->phase == 2) {  // RELEASE phase
        if (g_run->isUnicode) {
            SendUnicodeChar(g_run->currentItem.cp, true);
        } else {
            SendTaggedKey(g_run->resolvedVk, g_run->resolvedScan, KEYEVENTF_KEYUP);
        }
        g_run->keyHeld = false;
        ReleaseActiveModifiers();

        g_run->typist->OnCharTyped(g_run->currentItem.cp, g_run->currentItem.count);

        int64_t now = NowUs();
        if (now - g_run->lastProgressUs > 120000) {
            g_run->lastProgressUs = now;
            int64_t elapsedMs = (now - g_run->startUs) / 1000;
            int eta = g_run->typist->EtaRemainingMs(elapsedMs);
            std::string etaStr = ghost_logic::FormatEta(eta);
            UpdateGhostUi(ghost_logic::FormatStatus(g_run->typist->TypedChars(), g_run->typist->TotalChars()),
                          etaStr, "Cancel", "typing");
        }

        g_run->phase = 0;  // Back to CHAR for next keystroke
        int nextMs = std::max(1, g_run->currentInterval.restMs + g_run->currentInterval.postRestExtraMs);
        if (!SetTimer(overlay::Hwnd(), kGhostStepTimerId, nextMs, nullptr)) Cancel();
    }
}

void OnSettleTick() {
    if (!g_isTyping || !g_run) return;
    KillTimer(overlay::Hwnd(), kGhostSettleTimerId);

    bool modHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) || (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
                   (GetAsyncKeyState(VK_MENU) & 0x8000) || (GetAsyncKeyState(VK_LWIN) & 0x8000) ||
                   (GetAsyncKeyState(VK_RWIN) & 0x8000);

    g_run->settleRetries++;
    if (modHeld && g_run->settleRetries < 24) {
        if (!SetTimer(overlay::Hwnd(), kGhostSettleTimerId, 10, nullptr)) Cancel();
        return;
    }

    if (modHeld) {
        Cancel();
        overlay::ShowToast("Release modifier keys and try again");
        return;
    }

    g_run->startUs = NowUs();
    g_run->lastProgressUs = g_run->startUs;
    g_run->lastEtaUs = g_run->startUs;

    // Settle delay 120 - 349 ms
    ghost_logic::Rng rng(NowUs());
    int delay = rng.Range(120, 349);
    if (!SetTimer(overlay::Hwnd(), kGhostStepTimerId, delay, nullptr)) Cancel();
}

void Run() {
    if (g_countdownActive) {
        KillTimer(overlay::Hwnd(), kGhostCountdownTimerId);
        g_countdownActive = false;
    }
    int64_t now = NowUs();
    if (now - g_lastRunUs < 200000) {
        Log("GHOST", "Debounced double trigger (<200ms).");
        return;
    }
    g_lastRunUs = now;

    if (g_isTyping) {
        Cancel();
        return;
    }

    const std::string& rawText = app::TestEntryText();
    if (rawText.empty()) {
        Log("GHOST", "Triggered with empty input box.");
        overlay::ShowToast(ghost_logic::kToastNothingToType);
        UpdateGhostUi("Ready", "", "Start", "failed");
        return;
    }

    std::string text = ghost_logic::NormalizeText(rawText);
    if (text.empty()) {
        Log("GHOST", "Triggered with empty normalized text.");
        overlay::ShowToast(ghost_logic::kToastNothingToType);
        UpdateGhostUi("Ready", "", "Start", "failed");
        return;
    }

    const AppConfig& cfg = config::Get();
    auto cps = ghost_logic::Utf8ToCodepoints(text);
    int maxChars = (cfg.ghostMaxChars > 0) ? cfg.ghostMaxChars : 1000000;
    if (static_cast<int>(cps.size()) > maxChars) {
        Log("GHOST", "Text exceeds limit (%zu > %d chars).", cps.size(), maxChars);
        char toastBuf[128];
        snprintf(toastBuf, sizeof(toastBuf), "Text too long (max %d)", maxChars);
        overlay::ShowToast(toastBuf);
        UpdateGhostUi("Ready", "", "Start", "failed");
        return;
    }

    // Release grabs and set typing target to NONE
    input::ReleaseGrab();
    hotkey::SetClipboardShield(false);
    input::SetTypingTarget(input::TypingTarget::None);

    HWND target = GetForegroundWindow();
    HWND overlayHwnd = overlay::Hwnd();
    if (!target || target == overlayHwnd || IsChild(overlayHwnd, target) || target == GetDesktopWindow() ||
        target == GetShellWindow() || !IsWindowVisible(target)) {
        Log("GHOST", "No valid external focus window found.");
        overlay::ShowToast(ghost_logic::kToastNoFocusedApp);
        UpdateGhostUi("Ready", "", "Start", "failed");
        return;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    HANDLE hTargetProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hTargetProc) {
        HANDLE hCurrentProc = GetCurrentProcess();
        if (IsProcessElevated(hTargetProc) && !IsProcessElevated(hCurrentProc) && !HasUIAccess()) {
            CloseHandle(hTargetProc);
            LogErr("GHOST", "Target process runs as administrator; aborting due to UIPI.");
            overlay::ShowToast(ghost_logic::kToastTargetElevated);
            UpdateGhostUi("Ready", "", "Start", "failed");
            return;
        }
        CloseHandle(hTargetProc);
    }

    g_sendFailed = false;
    g_run = std::make_unique<TypingRun>();
    g_run->targetWindow = target;
    g_run->appName = GetProcessBaseName(target);
    g_run->capsWasOn = hotkey::CapsLockOn();
    if (g_run->capsWasOn) {
        SendTaggedKey(VK_CAPITAL, 0, 0);
        SendTaggedKey(VK_CAPITAL, 0, KEYEVENTF_KEYUP);
    }

    g_run->typist = std::make_unique<ghost_logic::Typist>(text, GetTiming(), NowUs());
    g_run->phase = 0;
    g_run->settleRetries = 0;

    KillTimer(overlay::Hwnd(), kGhostGuardDisengageTimerId);
    g_isTyping = true;
    g_guardActive = true;
    hotkey::SetTypingGuard(true);

    UpdateGhostUi(ghost_logic::FormatStatus(0, g_run->typist->TotalChars()), "", "Cancel", "typing");
    Log("GHOST", "Ghost Writer typing started for %zu characters into '%s'.", g_run->typist->TotalChars(),
        g_run->appName.c_str());

    if (g_sendFailed) {
        Cancel();
        return;
    }
    if (!SetTimer(overlay::Hwnd(), kGhostProgressTimerId, 500, nullptr))
        LogErr("GHOST", "Progress timer unavailable (%lu)", GetLastError());
    // Begin settle phase
    if (!SetTimer(overlay::Hwnd(), kGhostSettleTimerId, 10, nullptr)) Cancel();
}

}  // namespace ghost
}  // namespace cronq
