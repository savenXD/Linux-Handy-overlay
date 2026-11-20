#include "overlay.h"

#include "app.h"
#include "chat_view.h"
#include "ui_bridge.h"
#include "jsonlite.h"
#include "config.h"
#include "geometry.h"
#include "god_copy.h"
#include "hotkey.h"
#include "input.h"
#include "raise.h"
#include "snap.h"
#include "theme.h"

#include <wtsapi32.h>
#include <cstdio>
#include <climits>
#include <cstdlib>
#include <vector>
#include <algorithm>

namespace cronq {
namespace overlay {

namespace {

constexpr DWORD kAffinityExclude = 0x00000011;  // WDA_EXCLUDEFROMCAPTURE

const wchar_t* const kClassName = L"CronqOverlayWindow";
constexpr UINT_PTR kDragTimerId = 2;
constexpr UINT_PTR kTypingHoverTimerId = 3;

HWND g_hwnd = nullptr;
HINSTANCE g_inst = nullptr;
UINT g_dpi = 96;

int g_x = 0, g_y = 0;
int g_w = 0, g_h = 0;
double g_opacity = 1.0;
bool g_visible = false;
bool g_userMoved = false;
bool g_maximized = false;
int g_prevX = 0, g_prevY = 0, g_prevW = 0, g_prevH = 0;
std::string g_app = "home";
UINT g_msgTaskbarCreated = 0;

int S(int v) { return MulDiv(v, (int)g_dpi, 96); }

// ---------------------------------------------------------------- monitors / DPI

struct Mon {
    RECT full{0, 0, 1920, 1080};
    RECT work{0, 0, 1920, 1080};
    HMONITOR handle = nullptr;
};

Mon Describe(HMONITOR h) {
    Mon m;
    m.handle = h;
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (h && GetMonitorInfoW(h, &mi)) {
        m.full = mi.rcMonitor;
        m.work = mi.rcWork;
    }
    return m;
}

Mon PrimaryMonitor() {
    POINT origin = {0, 0};
    return Describe(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY));
}

BOOL CALLBACK EnumMonitorsProc(HMONITOR h, HDC, LPRECT, LPARAM lp) {
    reinterpret_cast<std::vector<HMONITOR>*>(lp)->push_back(h);
    return TRUE;
}

Mon ConfiguredMonitor() {
    const AppConfig& cfg = config::Get();
    if (EqNoCase(cfg.monitor, "primary")) return PrimaryMonitor();
    char* end = nullptr;
    long requested = strtol(cfg.monitor.c_str(), &end, 10);
    if (!end || end == cfg.monitor.c_str() || *end || requested < 0 || requested > INT_MAX)
        return PrimaryMonitor();
    int idx = static_cast<int>(requested);
    std::vector<HMONITOR> all;
    EnumDisplayMonitors(nullptr, nullptr, EnumMonitorsProc, reinterpret_cast<LPARAM>(&all));
    if (!all.empty()) {
        if (idx >= 0 && idx < static_cast<int>(all.size())) return Describe(all[static_cast<size_t>(idx)]);
    }
    return PrimaryMonitor();
}

UINT MonitorDpi(HMONITOR h) {
    typedef HRESULT(WINAPI * GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
    static GetDpiForMonitorFn fn = [] {
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        return shcore ? reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor"))
                      : nullptr;
    }();
    UINT dx = 96, dy = 96;
    if (fn && h && SUCCEEDED(fn(h, 0 /* MDT_EFFECTIVE_DPI */, &dx, &dy)) && dx >= 96) return dx;
    return 96;
}

void DefaultDimensions(const Mon& mon, int* width, int* height) {
    const auto& cfg = config::Get();
    *width = S(cfg.width);
    *height = S(cfg.height);
    geometry::FitPhone({mon.work.left, mon.work.top, mon.work.right, mon.work.bottom},
                       S(cfg.margin), width, height);
}

// The position is computed against the work area so the taskbar stays reachable.
void CalcPosition(int* outX, int* outY) {
    const AppConfig& cfg = config::Get();
    Mon mon = ConfiguredMonitor();
    g_dpi = MonitorDpi(mon.handle);

    int w = g_w > 0 ? g_w : S(cfg.width);
    int h = g_h > 0 ? g_h : S(cfg.height);
    int m = S(cfg.margin);
    const RECT& r = mon.work;

    geometry::Rect workRect{r.left, r.top, r.right, r.bottom};
    geometry::CalcCorner(workRect, w, h, m, cfg.corner, outX, outY);
    Log("WINDOW", "Calculated position: (%d, %d) on work area %ldx%ld+%ld+%ld (corner=%s, margin=%d)",
        outX ? *outX : 0, outY ? *outY : 0, r.right - r.left, r.bottom - r.top, r.left, r.top,
        cfg.corner.c_str(), m);
}

// ---------------------------------------------------------------- window plumbing

void UpdateRegion() {
    if (!g_hwnd) return;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int r = theme::IsCamouflage() ? 0 : S(kCornerRadius);
    HRGN rgn = CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, r * 2, r * 2);
    SetWindowRgn(g_hwnd, rgn, TRUE);  // the window owns rgn from here on
}

void ApplyGeometry() {
    if (!g_hwnd) return;
    SetWindowPos(g_hwnd, nullptr, g_x, g_y, g_w, g_h, SWP_NOACTIVATE | SWP_NOZORDER);
    hotkey::UpdateOverlayRect(g_x, g_y, g_x + g_w, g_y + g_h);
    chat_view::ResizeHost();
}

void ApplyAlpha() {
    if (!g_hwnd) return;
    int a = (int)(g_opacity * 255.0 + 0.5);
    if (a < 1) a = 1;
    if (a > 255) a = 255;
    // The minimal Light canvas is keyed out; opacity still applies to its text.
    auto bg=theme::Get().background;
    SetLayeredWindowAttributes(g_hwnd, RGB(bg.r,bg.g,bg.b), (BYTE)a,
                               LWA_ALPHA | (theme::IsCamouflage() ? LWA_COLORKEY : 0));
}

void MoveTo(int x, int y) {
    g_x = x;
    g_y = y;
    if (g_hwnd) {
        SetWindowPos(g_hwnd, nullptr, g_x, g_y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        hotkey::UpdateOverlayRect(g_x, g_y, g_x + g_w, g_y + g_h);
        chat_view::ResizeHost();
    }
}

void SetCompactMode(bool compact) {
    if (!g_hwnd || g_maximized) return;
    Mon mon = Describe(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST));
    int defaultW, defaultH;
    DefaultDimensions(mon, &defaultW, &defaultH);
    int target = compact ? std::min(S(kCompactHeight), defaultH) : defaultH;
    if (g_h == target) return;

    int oldH = g_h;
    g_h = target;
    if (g_userMoved) {
        if (config::Get().corner.find("top-") != 0) g_y -= (target - oldH);
        g_y = std::clamp(g_y, int(mon.work.top), std::max(int(mon.work.top), int(mon.work.bottom) - target));
    } else {
        CalcPosition(&g_x, &g_y);
    }
    ApplyGeometry();
    QueueDraw();
    Log("WINDOW", "Set compact mode: %s (height=%d, pos_y=%d)", compact ? "COMPACT" : "EXPANDED", g_h, g_y);
}

// ---------------------------------------------------------------- layout / hit testing

enum Hit {
    HitNone = 0,
    HitHandle,
    HitPillMode,
    HitPillGhost,
    HitGrip,
    HitRecovery, HitPrevious, HitOverview, HitReload, HitReport,
    HitEdge,  // see edge mask
};
constexpr int kEdgeL = 1, kEdgeR = 2, kEdgeT = 4, kEdgeB = 8;

struct Layout {
    int header;
    RECT pillMode, pillGhost, handleHit, grip, footer[4];
};

RECT R(int l, int t, int r, int b) {
    RECT rc = {l, t, r, b};
    return rc;
}

RECT g_recoveryHit{};
Layout MakeLayout(int w, int h) {
    Layout lay{};
    lay.header = S(36);
    lay.pillMode = R(S(12), S(4), S(36), S(32));
    lay.pillGhost = R(S(40), S(4), S(64), S(32));
    lay.handleHit = R(S(68), S(4), w-S(8), lay.header);
    lay.grip = R(w-S(12),h-S(12),w,h);
    const int col=(w-S(24))/4;
    for(int i=0;i<4;++i) lay.footer[i]=R(S(12)+i*col,h-S(39),S(12)+(i+1)*col-S(4),h-S(3));
    return lay;
}

bool In(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

int HitTest(int x, int y, int* edgeMask) {
    *edgeMask = 0;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    Layout lay = MakeLayout(rc.right, rc.bottom);
    if (theme::IsCamouflage()) {
        if (In(R(rc.right-S(64), S(4), rc.right-S(40), S(32)),x,y)) return HitPillMode;
        if (In(R(rc.right-S(36), S(4), rc.right-S(12), S(32)),x,y)) return HitPillGhost;
    }

    if (!theme::IsCamouflage()) {
    if (In(lay.pillMode, x, y)) return HitPillMode;
    if (In(lay.pillGhost, x, y)) return HitPillGhost;
    if (In(lay.handleHit, x, y)) return HitHandle;
    if (g_app=="home" && In(g_recoveryHit,x,y)) return HitRecovery;
    for(int i=0;i<4;++i) if(In(lay.footer[i],x,y)) return HitPrevious+i;
    if (In(lay.grip, x, y)) {
        *edgeMask = kEdgeR | kEdgeB;
        return HitGrip;
    }

    }
    int border = S(8);
    int mask = 0;
    if (x <= border)
        mask |= kEdgeL;
    else if (x >= rc.right - border)
        mask |= kEdgeR;
    if (y <= border)
        mask |= kEdgeT;
    else if (y >= rc.bottom - border)
        mask |= kEdgeB;
    if (mask) {
        *edgeMask = mask;
        return HitEdge;
    }
    return HitNone;
}

// ---------------------------------------------------------------- painting

COLORREF kBg = RGB(16, 18, 26);
COLORREF kHeaderBg = RGB(24, 27, 38);
COLORREF kText = RGB(232, 234, 242);
COLORREF kMuted = RGB(140, 146, 165);

void FillRound(HDC dc, const RECT& r, int radius, COLORREF fill) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius * 2, radius * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

void Line(HDC dc, int x1, int y1, int x2, int y2, COLORREF c, int width) {
    HPEN pen = CreatePen(PS_SOLID, width, c);
    HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, op);
    DeleteObject(pen);
}

HFONT MakeFont(int px, int weight) {
    return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void Text(HDC dc, HFONT font, const std::wstring& s, RECT r, COLORREF c, UINT flags) {
    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, c);
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}

void Paint(HDC wndDc) {
    if (theme::IsCamouflage()) {
        RECT rc; GetClientRect(g_hwnd,&rc);
        auto bg=theme::Get().background;
        HBRUSH key=CreateSolidBrush(RGB(bg.r,bg.g,bg.b)); FillRect(wndDc,&rc,key); DeleteObject(key);
        if (!chat_view::IsUiVisible()) {
            const bool typing = input::GetTypingTarget() == input::TypingTarget::Overlay &&
                                input::GetGrabStatus() == input::GrabStatus::Active;
            const int cx = rc.right - S(52), cy = S(18), d = S(7);
            if (typing) FillRound(wndDc, R(cx-d,cy-d,cx+d,cy+d), d, RGB(20,100,50));
            FillRound(wndDc, R(cx-d/2,cy-d/2,cx+(d+1)/2,cy+(d+1)/2), d/2,
                      typing ? RGB(34,197,94) : RGB(115,115,115));
            const int gx=rc.right-S(24);
            FillRound(wndDc,R(gx-d/2,cy-d/2,gx+(d+1)/2,cy+(d+1)/2),d/2,
                      input::GetGhostMode()==input::GhostMode::On ? RGB(168,85,247) : RGB(115,115,115));
        }
        return;
    }
    if (chat_view::IsUiVisible()) return;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;

    HDC dc = CreateCompatibleDC(wndDc);
    HBITMAP bmp = CreateCompatibleBitmap(wndDc, w, h);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);

    // Background + header
    const auto& palette=theme::Get();
    auto color=[](theme_logic::Color c) { return RGB(c.r,c.g,c.b); };
    kBg=color(palette.background); kHeaderBg=color(palette.background);
    kText=color(palette.text); kMuted=color(palette.muted);
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    Layout lay = MakeLayout(w, h);
    RECT headerRect = R(0, 0, w, lay.header);
    HBRUSH hb = CreateSolidBrush(kHeaderBg);
    FillRect(dc, &headerRect, hb);
    DeleteObject(hb);

    HFONT fSmall = MakeFont(11, FW_SEMIBOLD);
    HFONT fBody = MakeFont(12, FW_NORMAL);
    HFONT fTitle = MakeFont(w<S(290)?17:20, FW_SEMIBOLD);

    // Match the HTML shell when WebView2 is unavailable; never expose a debug panel.
    const bool typing = input::GetTypingTarget()==input::TypingTarget::Overlay &&
                        input::GetGrabStatus()==input::GrabStatus::Active;
    const bool ghost = input::GetGhostMode()==input::GhostMode::On;
    for(int i=0;i<2;++i) {
        const RECT& pill=i ? lay.pillGhost : lay.pillMode;
        int cx=(pill.left+pill.right)/2, cy=(pill.top+pill.bottom)/2, d=S(7);
        COLORREF dot=i ? (ghost ? RGB(168,85,247) : RGB(115,115,115))
                       : (typing ? RGB(34,197,94) : RGB(115,115,115));
        FillRound(dc,R(cx-d/2,cy-d/2,cx+(d+1)/2,cy+(d+1)/2),d/2,dot);
    }
    const int bodyBottom=h-S(46);
    const bool compact=h<=S(240);
    int y=lay.header+std::max(S(6),(bodyBottom-lay.header-S(compact?85:143))/2);
    if(!compact) {
        int cx=w/2;
        Line(dc,cx-S(12),y,cx+S(4),y,kMuted,1);
        Line(dc,cx-S(12),y,cx-S(12),y+S(32),kMuted,1);
        Line(dc,cx-S(12),y+S(32),cx+S(12),y+S(32),kMuted,1);
        Line(dc,cx+S(12),y+S(10),cx+S(12),y+S(32),kMuted,1);
        Line(dc,cx+S(4),y,cx+S(12),y+S(10),kMuted,1);
        Line(dc,cx-S(5),y+S(24),cx,y+S(21),kMuted,1);
        Line(dc,cx,y+S(21),cx+S(5),y+S(24),kMuted,1);
        y+=S(46);
    }
    Text(dc,fSmall,L"HTTP ERROR 404",R(S(12),y,w-S(12),y+S(15)),kMuted,DT_CENTER|DT_SINGLELINE);
    y+=S(23);
    Text(dc,fTitle,L"This page isn’t available",R(S(10),y,w-S(10),y+S(26)),kText,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    y+=S(34);
    Text(dc,fBody,L"The address may have changed.",R(S(8),y,w-S(8),y+S(18)),kMuted,DT_CENTER|DT_SINGLELINE);
    y+=S(19);
    const wchar_t* line=L"Try recovery or reload the page.";
    Text(dc,fBody,line,R(S(8),y,w-S(8),y+S(18)),kMuted,DT_CENTER|DT_SINGLELINE);
    HGDIOBJ oldFont=SelectObject(dc,fBody);
    SIZE total{},prefix{},word{};
    GetTextExtentPoint32W(dc,line,(int)wcslen(line),&total);
    GetTextExtentPoint32W(dc,L"Try ",4,&prefix);
    GetTextExtentPoint32W(dc,L"recovery",8,&word);
    SelectObject(dc,oldFont);
    int left=(w-total.cx)/2+prefix.cx;
    g_recoveryHit=R(left,y,left+word.cx,y+S(18));
    for(int i=0;i<4;++i) {
        const RECT& r=lay.footer[i];
        const bool enabled=i!=0||g_app!="home";
        const int cx=(r.left+r.right)/2, cy=h-S(7), d=S(2);
        int gray=palette.light?184:71;
        if(!enabled) gray=palette.light?225:25;
        FillRound(dc,R(cx-d/2,cy-d/2,cx+(d+1)/2,cy+(d+1)/2),d/2,RGB(gray,gray,gray));
    }

    DeleteObject(fSmall);
    DeleteObject(fBody);
    DeleteObject(fTitle);

    BitBlt(wndDc, 0, 0, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// ---------------------------------------------------------------- mouse interaction

enum DragKind { DragNone, DragMove, DragResize };
DragKind g_drag = DragNone;
int g_dragEdges = 0;
POINT g_dragStartCursor = {0, 0};
int g_dragStartX = 0, g_dragStartY = 0, g_dragStartW = 0, g_dragStartH = 0;
int g_pressHit = HitNone;

void ToggleMaximizedHeight() {
    Mon mon = Describe(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST));  // the screen it is on
    if (!g_maximized) {
        g_prevW = g_w;
        g_prevH = g_h;
        g_prevX = g_x;
        g_prevY = g_y;
        int maxH = (mon.work.bottom - mon.work.top) - S(24);
        MoveTo(g_x, mon.work.top + S(12));
        SetSize(g_w, maxH);
        g_maximized = true;
        ShowToast("Maximized height");
    } else {
        MoveTo(g_prevX, g_prevY);
        SetSize(g_prevW, g_prevH);
        g_maximized = false;
        ShowToast("Restored size");
    }
}

void OnLButtonDown(int x, int y) {
    int edges = 0;
    int hit = HitTest(x, y, &edges);
    g_pressHit = hit;
    if (hit == HitNone) return;

    if (hit == HitHandle || hit == HitGrip || hit == HitEdge) {
        GetCursorPos(&g_dragStartCursor);
        g_dragStartX = g_x;
        g_dragStartY = g_y;
        g_dragStartW = g_w;
        g_dragStartH = g_h;
        g_drag = (hit == HitHandle) ? DragMove : DragResize;
        g_dragEdges = edges;
    }
    SetCapture(g_hwnd);
}

void OnMouseMove() {
    if (g_drag == DragNone) return;
    POINT p;
    GetCursorPos(&p);
    int dx = p.x - g_dragStartCursor.x;
    int dy = p.y - g_dragStartCursor.y;

    if (g_drag == DragMove) {
        g_userMoved = true;
        MoveTo(g_dragStartX + dx, g_dragStartY + dy);
        return;
    }

    int newW = g_dragStartW, newH = g_dragStartH, newX = g_dragStartX, newY = g_dragStartY;
    int minW = S(kMinWidth), minH = S(kMinHeight);
    if (g_dragEdges & kEdgeR) {
        newW = g_dragStartW + dx;
    } else if (g_dragEdges & kEdgeL) {
        newW = g_dragStartW - dx;
        if (newW < minW) newW = minW;
        newX = g_dragStartX + (g_dragStartW - newW);
    }
    if (g_dragEdges & kEdgeB) {
        newH = g_dragStartH + dy;
    } else if (g_dragEdges & kEdgeT) {
        newH = g_dragStartH - dy;
        if (newH < minH) newH = minH;
        newY = g_dragStartY + (g_dragStartH - newH);
    }
    g_x = newX;
    g_y = newY;
    SetSize(newW, newH);
}

void OnLButtonUp(int x, int y) {
    bool wasCaptured = (GetCapture() == g_hwnd);
    int pressed = g_pressHit;
    bool dragged = (g_drag != DragNone);
    g_drag = DragNone;
    g_pressHit = HitNone;
    if (wasCaptured) ReleaseCapture();
    if (dragged || pressed == HitNone) return;

    int edges = 0;
    if (HitTest(x, y, &edges) != pressed) return;  // released somewhere else: cancel
    switch (pressed) {
        case HitPrevious:
            if(g_app!="home") Back();
            break;
        case HitOverview: SwitchApp("home"); break;
        case HitReload: UniversalReset(); break;
        case HitReport: snap::Run(); break;
        case HitPillMode:
            input::ToggleTypingTarget();
            break;
        case HitPillGhost:
            input::ToggleGhostMode();
            break;
        default:
            break;
    }
}

bool OnSetCursor() {
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(g_hwnd, &p);
    int edges = 0;
    int hit = (g_drag == DragMove) ? HitHandle : HitTest(p.x, p.y, &edges);
    if (g_drag == DragResize) edges = g_dragEdges, hit = HitEdge;

    LPCWSTR cur = IDC_ARROW;
    if (hit == HitHandle) {
        cur = IDC_SIZEALL;
    } else if (hit == HitPillMode ||
               hit == HitPillGhost || (hit>=HitRecovery && hit<=HitReport)) {
        cur = IDC_HAND;
    } else if (hit == HitGrip || hit == HitEdge) {
        bool l = edges & kEdgeL, r = edges & kEdgeR, t = edges & kEdgeT, b = edges & kEdgeB;
        if ((l && t) || (r && b))
            cur = IDC_SIZENWSE;
        else if ((r && t) || (l && b))
            cur = IDC_SIZENESW;
        else if (l || r)
            cur = IDC_SIZEWE;
        else if (t || b)
            cur = IDC_SIZENS;
    }
    SetCursor(LoadCursorW(nullptr, cur));
    return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;  // clicking the overlay never takes focus from the app below
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            Paint(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            UpdateRegion();
            chat_view::ResizeHost();
            ui::PushState();
            return 0;
        case WM_CRONQ_SNAP_DONE:
            snap::OnEncoded(reinterpret_cast<void*>(lp));
            return 0;
        case WM_CRONQ_WEBVIEW_RECOVER:
            chat_view::Recover(static_cast<unsigned>(wp), static_cast<unsigned>(lp));
            return 0;
        case WM_LBUTTONDOWN:
            OnLButtonDown((short)LOWORD(lp), (short)HIWORD(lp));
            return 0;
        case WM_LBUTTONDBLCLK: {
            int x = (short)LOWORD(lp), y = (short)HIWORD(lp), edges = 0;
            const int hit=HitTest(x,y,&edges);
            if(hit==HitRecovery && g_app=="home") {
                g_pressHit=HitNone; SwitchApp("chatgpt");
            } else if (hit == HitHandle) {
                g_pressHit = HitNone;
                ToggleMaximizedHeight();
            } else {
                OnLButtonDown(x, y);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            OnLButtonUp((short)LOWORD(lp), (short)HIWORD(lp));
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove();
            return 0;
        case WM_CAPTURECHANGED:
            g_drag = DragNone;
            g_pressHit = HitNone;
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                OnSetCursor();
                return TRUE;
            }
            break;
        case WM_TIMER:
            if(wp==kTypingHoverTimerId){chat_view::CheckTypingHover();return 0;}
            if (wp == kDragTimerId) {
                if (g_drag == DragNone || !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    KillTimer(hwnd, kDragTimerId);
                    g_drag = DragNone;
                } else
                    OnMouseMove();
                return 0;
            }
            if (wp == snap::kSnapCaptureTimerId) {
                snap::OnSnapTimeout();
                return 0;
            }
            if (wp == snap::kSnapRevertTimerId) {
                snap::OnSnapRevert();
                return 0;
            }
            if (wp == snap::kSnapClipboardTimerId) {
                snap::OnClipboardRetry();
                return 0;
            }
            if (wp == snap::kSnapWorkerTimerId) {
                snap::OnWorkerPoll();
                return 0;
            }
            break;
        case WM_DISPLAYCHANGE:
            if (g_visible) {
                if (!g_userMoved && !g_maximized) {
                    Reposition();
                } else {
                    Mon mon = Describe(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST));
                    if (g_x + g_w < mon.work.left + 50 || g_x > mon.work.right - 50 ||
                        g_y + g_h < mon.work.top + 50 || g_y > mon.work.bottom - 50) {
                        Reposition();
                    }
                }
            }
            return 0;
        case WM_SETTINGCHANGE:
            if (wp == SPI_SETWORKAREA && g_visible && !g_userMoved && !g_maximized) Reposition();
            return 0;
        case WM_DPICHANGED:
            g_dpi = HIWORD(wp);
            ApplyGeometry();
            QueueDraw();
            return 0;  // preserve the current physical rectangle; scale controls to the new monitor
        case WM_POWERBROADCAST:
            if (wp == PBT_APMSUSPEND) {
                input::ReleaseGrab();
            }
            if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
                hotkey::Reinstall();
                Log("WINDOW", "System resumed from sleep: re-installing hooks and re-asserting topmost.");
                if (g_visible) {
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
            return TRUE;
        case WM_WTSSESSION_CHANGE:
            if (wp == WTS_SESSION_LOCK) {
                input::ReleaseGrab();
            }
            if (wp == WTS_SESSION_UNLOCK) {
                hotkey::Reinstall();
                Log("WINDOW", "Session unlocked: re-installing hooks and re-asserting topmost.");
                if (g_visible) {
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
            return 0;
        case WM_CLOSE:
            PostMessageW(hwnd, WM_CRONQ_QUIT, 0, 0);
            return 0;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wp) PostMessageW(hwnd, WM_CRONQ_QUIT, 0, 0);
            return 0;
        case WM_CRONQ_INPUT_STATE:
        case WM_CRONQ_IPC:
        case WM_CRONQ_HOTKEY:
        case WM_CRONQ_QUIT:
        case WM_CRONQ_ALTFIX:
        case WM_CRONQ_CLICK:
        case WM_CRONQ_KEY:
        case WM_CRONQ_CLIPBOARD:
            return app::OnAppMessage(msg, wp, lp);
        default:
            if (g_msgTaskbarCreated && msg == g_msgTaskbarCreated) {
                Log("WINDOW", "Explorer/Taskbar recreated: re-asserting topmost.");
                if (g_visible) {
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    if (!g_userMoved && !g_maximized) Reposition();
                }
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

// ---------------------------------------------------------------- public API

static bool g_captureHidden = false;
static bool g_refusedForCapture = false;
bool RefusedForCapture() { return g_refusedForCapture; }

unsigned CaptureAffinity() { return g_captureHidden ? kAffinityExclude : 0u; }

bool SetCaptureHidden(bool hidden) {
    if (!g_hwnd) return false;
    if (!SetWindowDisplayAffinity(g_hwnd, hidden ? kAffinityExclude : 0u)) {
        LogErr("WINDOW", "SetWindowDisplayAffinity failed (%lu); capture exclusion needs Windows 10 2004+.",
               GetLastError());
        return false;
    }
    g_captureHidden = hidden;
    return true;
}

namespace {

BOOL CALLBACK ProtectEnumProc(HWND hwnd, LPARAM) {
    DWORD cur = 0;
    if (GetWindowDisplayAffinity(hwnd, &cur) && cur != kAffinityExclude &&
        !SetWindowDisplayAffinity(hwnd, kAffinityExclude))
        LogErr("WINDOW", "Could not exclude window 0x%p from capture (%lu)", static_cast<void*>(hwnd), GetLastError());
    return TRUE;
}

}  // namespace

// Windows only lets a process exclude its own windows from capture, and Cronq creates all of its
// top-level windows (overlay, kill prompt) on this UI thread. WebView2's own popups belong to
// msedgewebview2.exe and cannot be excluded: chat_view.cpp disables them instead.
void ProtectAllWindows() {
    if (!g_captureHidden) return;
    EnumThreadWindows(GetCurrentThreadId(), ProtectEnumProc, 0);
}

bool Init(bool startVisible) {
    g_inst = GetModuleHandleW(nullptr);
    const AppConfig& cfg = config::Get();

    Mon mon = ConfiguredMonitor();
    g_dpi = MonitorDpi(mon.handle);
    DefaultDimensions(mon, &g_w, &g_h);
    g_h = std::min(g_h, S(kCompactHeight));
    g_opacity = cfg.opacity;
    CalcPosition(&g_x, &g_y);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        LogErr("WINDOW", "RegisterClassEx failed (%lu).", GetLastError());
        return false;
    }

    // TOPMOST: above normal windows. TOOLWINDOW: no taskbar button / Alt+Tab entry.
    // NOACTIVATE: never becomes the foreground window. LAYERED: opacity + click-through.
    DWORD ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED;
    g_hwnd = CreateWindowExW(ex, kClassName, L"Cronq", WS_POPUP, g_x, g_y, g_w, g_h, nullptr, nullptr, g_inst,
                             nullptr);
    if (!g_hwnd) {
        LogErr("WINDOW", "CreateWindowEx failed (%lu).", GetLastError());
        return false;
    }
    ApplyAlpha();
    UpdateRegion();
    if (cfg.hideFromCapture && !SetCaptureHidden(true)) {
        // Fail closed: never run visible to recorders when the user asked for protection.
        MessageBoxW(nullptr,
                    L"Cronq cannot hide itself from screen capture on this Windows version (needs Windows 10 2004 or "
                    L"newer), so it will not start.\n\nTo run anyway, set hide_from_capture = false under [Privacy] in "
                    L"config.ini.",
                    L"Cronq", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
        g_refusedForCapture = true;
        return false;
    }
    hotkey::UpdateOverlayRect(g_x, g_y, g_x + g_w, g_y + g_h);
    Log("WINDOW", "Created overlay window HWND 0x%llx (%dx%d at %d,%d, dpi=%u)",
        (unsigned long long)(UINT_PTR)g_hwnd, g_w, g_h, g_x, g_y, g_dpi);

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WTSRegisterSessionNotification(g_hwnd, NOTIFY_FOR_THIS_SESSION);

    if (startVisible) Show();
    return true;
}

void Cleanup() {
    if (g_hwnd) {
        WTSUnRegisterSessionNotification(g_hwnd);
        KillTimer(g_hwnd, kTypingHoverTimerId);
        KillTimer(g_hwnd, kDragTimerId);
    }
    raise::Stop();
    if (g_hwnd) {
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
    UnregisterClassW(kClassName, g_inst);
}

HWND Hwnd() { return g_hwnd; }
void RefreshAppearance() { ApplyAlpha(); UpdateRegion(); QueueDraw(); }

// Keeps a saved position reachable: a monitor may have been unplugged or rearranged while hidden.
void KeepOnScreen() {
    RECT want = {g_x, g_y, g_x + g_w, g_y + g_h};
    Mon mon = Describe(MonitorFromRect(&want, MONITOR_DEFAULTTONEAREST));
    const RECT& r = mon.work;
    RECT inter;
    if (IntersectRect(&inter, &want, &r) && (inter.right - inter.left) >= S(80) && (inter.bottom - inter.top) >= S(60))
        return;  // enough of it is visible to grab and drag back
    g_w = std::min(g_w, int(r.right - r.left));
    g_h = std::min(g_h, int(r.bottom - r.top));
    g_x = std::clamp(g_x, int(r.left), int(r.right) - g_w);
    g_y = std::clamp(g_y, int(r.top), int(r.bottom) - g_h);
    Log("WINDOW", "Saved position was off-screen; moved to (%d, %d)", g_x, g_y);
}

void Show() {
    if (!g_hwnd) return;
    if (!g_maximized && !g_userMoved) CalcPosition(&g_x, &g_y);
    else KeepOnScreen();
    SetWindowPos(g_hwnd, HWND_TOPMOST, g_x, g_y, g_w, g_h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_visible = true;
    SetTimer(g_hwnd,kTypingHoverTimerId,100,nullptr);
    hotkey::SetOverlayVisible(true);
    hotkey::UpdateOverlayRect(g_x, g_y, g_x + g_w, g_y + g_h);
    chat_view::Resume();
    Log("WINDOW", "Overlay shown (topmost, no-activate at (%d, %d))", g_x, g_y);
    QueueDraw();
    raise::Start();
}

void Hide() {
    if (!g_hwnd) return;
    KillTimer(g_hwnd,kTypingHoverTimerId);
    raise::Stop();
    ShowWindow(g_hwnd, SW_HIDE);
    g_visible = false;
    hotkey::SetOverlayVisible(false);
    hotkey::UpdateOverlayRect(0, 0, 0, 0);
    chat_view::Pause();
    Log("WINDOW", "Overlay hidden");
}

void Toggle() {
    if (g_visible)
        Hide();
    else
        Show();
}

bool IsVisible() { return g_visible; }

void Reposition() {
    if (!g_hwnd) return;
    g_userMoved = false;
    CalcPosition(&g_x, &g_y);
    ApplyGeometry();
    QueueDraw();
    Log("WINDOW", "Overlay repositioned to (%d, %d)", g_x, g_y);
}

void DockBottom(bool right) {
    if (!g_hwnd) return;
    const std::string corner=right ? "bottom-right" : "bottom-left";
    const bool saved=config::SetCorner(corner);
    if (g_maximized) {
        g_w=g_prevW>0 ? g_prevW : S(kWidth);
        g_h=g_prevH>0 ? g_prevH : S(kHeight);
        g_maximized=false;
    }
    Mon mon=ConfiguredMonitor();g_dpi=MonitorDpi(mon.handle);
    geometry::CalcCorner({mon.work.left,mon.work.top,mon.work.right,mon.work.bottom},
                         g_w,g_h,S(config::Get().margin),corner,&g_x,&g_y);
    g_userMoved=!saved;
    ApplyGeometry();QueueDraw();
    if (!saved) ShowToast("Moved; could not save corner");
    Log("WINDOW","Docked %s at (%d,%d), saved=%d",corner.c_str(),g_x,g_y,saved);
}

void ApplyConfig() {
    if (!g_hwnd) return;
    const AppConfig& cfg = config::Get();
    Mon mon = ConfiguredMonitor();
    g_dpi = MonitorDpi(mon.handle);
    DefaultDimensions(mon, &g_w, &g_h);
    if (g_app == "home") g_h = std::min(g_h, S(kCompactHeight));
    g_opacity = cfg.opacity;
    if (cfg.hideFromCapture != g_captureHidden) SetCaptureHidden(cfg.hideFromCapture);
    g_maximized = false;
    g_userMoved = false;
    CalcPosition(&g_x, &g_y);
    ApplyAlpha();
    ApplyGeometry();
    QueueDraw();
    Log("WINDOW", "Applied config: %dx%d, opacity=%.2f, pos=(%d,%d)", g_w, g_h, g_opacity, g_x, g_y);
}

void SetOpacity(double opacity) {
    if (opacity < 0.20) opacity = 0.20;
    if (opacity > 1.00) opacity = 1.00;
    g_opacity = opacity;
    ApplyAlpha();
    char toast[64];
    snprintf(toast, sizeof(toast), "Opacity: %d%%", (int)(g_opacity * 100.0 + 0.5));
    ShowToast(toast);
}

double GetOpacity() { return g_opacity; }

void SetSize(int width, int height) {
    if (!g_hwnd) return;
    Mon mon = Describe(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST));
    int screenW = mon.full.right - mon.full.left;
    int screenH = mon.full.bottom - mon.full.top;

    if (width < S(kMinWidth)) width = S(kMinWidth);
    if (width > screenW) width = screenW;
    if (height < S(kMinHeight)) height = S(kMinHeight);
    if (height > screenH) height = screenH;

    if (width == g_w && height == g_h) {
        SetWindowPos(g_hwnd, nullptr, g_x, g_y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        return;
    }
    g_w = width;
    g_h = height;
    ApplyGeometry();
    QueueDraw();
}

void SetFullscreen() {
    if (!g_hwnd) return;
    Mon mon = Describe(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST));
    int monW = mon.full.right - mon.full.left;
    int monH = mon.full.bottom - mon.full.top;

    if (g_w == monW && g_h == monH) {
        int restW = g_prevW > 0 ? g_prevW : S(kWidth);
        int restH = g_prevH > 0 ? g_prevH : S(kHeight);
        MoveTo(g_prevX, g_prevY);
        SetSize(restW, restH);
        g_maximized = false;
        ShowToast("Restored size");
    } else {
        g_prevW = g_w;
        g_prevH = g_h;
        g_prevX = g_x;
        g_prevY = g_y;
        MoveTo(mon.full.left, mon.full.top);
        SetSize(monW, monH);
        g_maximized = true;
        ShowToast("Fullscreen mode");
    }
}

void SetDefaultSize() {
    if (!g_hwnd) return;
    Mon mon = ConfiguredMonitor();
    g_dpi = MonitorDpi(mon.handle);
    int defW, defH;
    DefaultDimensions(mon, &defW, &defH);
    if (g_app == "home") defH = std::min(defH, S(kCompactHeight));
    g_userMoved = false;
    SetSize(defW, defH);  // size first: the corner position depends on the final height
    CalcPosition(&g_x, &g_y);
    MoveTo(g_x, g_y);
    g_maximized = false;
    ShowToast("Default size restored");
}

void GetGeometry(int* x, int* y, int* w, int* h) {
    if (x) *x = g_x;
    if (y) *y = g_y;
    if (w) *w = g_w;
    if (h) *h = g_h;
}

void SetGhost(bool ghost) {
    if (!g_hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    if (ghost)
        ex |= WS_EX_TRANSPARENT;
    else
        ex &= ~(LONG_PTR)WS_EX_TRANSPARENT;
    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    Log("WINDOW", ghost ? "Overlay is click-through (GHOST)." : "Overlay is SOLID.");
    QueueDraw();
}

void QueueDraw() {
    ui::PushState();
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ShowToast(const std::string& msg) {
    // Preserve diagnostics without visual status bubbles in either renderer.
    Log("STATUS", "%s", msg.c_str());
}

void SwitchApp(const std::string& name) {
    if (EqNoCase(name, "chatgpt")) {
        g_app = "chatgpt";
        SetCompactMode(false);
    } else {
        g_app = "home";
        SetCompactMode(true);
    }
    chat_view::SetActiveApp(g_app);
    Log("WINDOW", "Switched view to '%s'.", g_app.c_str());
    QueueDraw();
}

double Scale() { return g_dpi / 96.0; }

void BeginExternalDrag(bool move, int edges) {
    if (!g_hwnd || input::GetGhostMode() == input::GhostMode::On) return;
    if (!GetCursorPos(&g_dragStartCursor)) return;
    g_dragStartX = g_x;
    g_dragStartY = g_y;
    g_dragStartW = g_w;
    g_dragStartH = g_h;
    g_dragEdges = edges;
    g_drag = move ? DragMove : DragResize;
    if (!SetTimer(g_hwnd, kDragTimerId, 8, nullptr)) {
        g_drag = DragNone;
        LogErr("WINDOW", "Drag timer failed (%lu)", GetLastError());
    }
}
void ToggleHeight() { ToggleMaximizedHeight(); }
void Back() {
    if (g_app == "chatgpt" && chat_view::CanGoBack())
        chat_view::GoBack();
    else
        SwitchApp("home");
}
void UniversalReset() {
    god_copy::Cleanup();
    input::ReleaseGrab();
    // Reset always restores normal application typing and the solid overlay.
    input::SetGhostMode(input::GhostMode::Off);
    g_userMoved = false;
    chat_view::UniversalReset();
    SwitchApp("home");
    ShowToast("Cronq reset to new state");
}

const char* CurrentApp() { return g_app.c_str(); }

}  // namespace overlay
}  // namespace cronq
