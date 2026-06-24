#include "weatherdesktop/SystemTray.h"

#ifdef _WIN32

#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <dwmapi.h>

// Tray hover notifications (NOTIFYICON_VERSION_4). Defined here in case the
// bundled SDK headers predate them.
#ifndef NIN_BALLOONUSERCLICK
#define NIN_BALLOONUSERCLICK (WM_USER + 5)
#endif
#ifndef NIN_POPUPOPEN
#define NIN_POPUPOPEN (WM_USER + 6)
#endif
#ifndef NIN_POPUPCLOSE
#define NIN_POPUPCLOSE (WM_USER + 7)
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace wd {

enum TrayMenuID {
    ID_TRAY_SHOW = 1001,
    ID_TRAY_REFRESH,
    ID_TRAY_QUIT
};

static const wchar_t* kFlyoutClass = L"WDTrayFlyout";

bool SystemTray::initialize(HWND hwnd, HICON icon) {
    hwnd_ = hwnd;

    ZeroMemory(&nid_, sizeof(nid_));
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = hwnd;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    nid_.hIcon = icon;
    wcscpy(nid_.szTip, L"Weather Desktop");

    if (!Shell_NotifyIconW(NIM_ADD, &nid_)) return false;

    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);

    initialized_ = true;
    return true;
}

void SystemTray::shutdown() {
    if (flyout_) {
        DestroyWindow(flyout_);
        flyout_ = nullptr;
    }
    if (!initialized_) return;
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    initialized_ = false;
}

void SystemTray::setQuickLook(const TrayQuickLook& q) {
    quick_ = q;
    // Repaint live if the flyout happens to be showing.
    if (flyout_ && IsWindowVisible(flyout_)) InvalidateRect(flyout_, nullptr, TRUE);
}

void SystemTray::setTooltip(const std::wstring& text) {
    if (!initialized_) return;
    wcsncpy(nid_.szTip, text.c_str(), 127);
    nid_.szTip[127] = L'\0';
    nid_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void SystemTray::showBalloon(const std::wstring& title, const std::wstring& msg) {
    if (!initialized_) return;
    nid_.uFlags = NIF_INFO;
    wcsncpy(nid_.szInfoTitle, title.c_str(), 63);
    nid_.szInfoTitle[63] = L'\0';
    wcsncpy(nid_.szInfo, msg.c_str(), 255);
    nid_.szInfo[255] = L'\0';
    nid_.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void SystemTray::showContextMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"Show Weather Desktop");
    AppendMenuW(menu, MF_STRING, ID_TRAY_REFRESH, L"Refresh Now");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_QUIT, L"Quit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
    // Win32 quirk: without this the menu can fail to dismiss on the first
    // click outside it.
    PostMessage(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

bool SystemTray::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_TRAYICON) {
        switch (LOWORD(lParam)) {
            case NIN_POPUPOPEN:
                // Version-4 packs the icon's screen anchor into wParam.
                showFlyout(GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam));
                return true;
            case NIN_POPUPCLOSE:
                hideFlyout();
                return true;
            case WM_LBUTTONDBLCLK:
                hideFlyout();
                pendingAction_ = MenuAction::Show;
                return true;
            case NIN_BALLOONUSERCLICK:  // clicked an alert toast -> open the app
                hideFlyout();
                pendingAction_ = MenuAction::Show;
                return true;
            case WM_RBUTTONUP:
                hideFlyout();
                showContextMenu();
                return true;
        }
    } else if (msg == WM_COMMAND) {
        switch (LOWORD(wParam)) {
            case ID_TRAY_SHOW:
                pendingAction_ = MenuAction::Show;
                return true;
            case ID_TRAY_REFRESH:
                pendingAction_ = MenuAction::Refresh;
                return true;
            case ID_TRAY_QUIT:
                pendingAction_ = MenuAction::Quit;
                return true;
        }
    }
    return false;
}

std::optional<SystemTray::MenuAction> SystemTray::pollAction() {
    auto action = pendingAction_;
    pendingAction_.reset();
    return action;
}

bool SystemTray::ensureFlyoutWindow() {
    if (flyout_) return true;

    HINSTANCE inst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = FlyoutWndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kFlyoutClass;
        if (!RegisterClassExW(&wc)) return false;
        registered = true;
    }

    // Topmost, no taskbar button, never takes focus -- it behaves like a tooltip.
    flyout_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        kFlyoutClass, L"", WS_POPUP,
        0, 0, 10, 10, nullptr, nullptr, inst, nullptr);
    if (!flyout_) return false;

    SetWindowLongPtrW(flyout_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Rounded corners on Windows 11 (ignored on older builds).
    int corner = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(flyout_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    return true;
}

void SystemTray::showFlyout(int anchorX, int anchorY) {
    if (!quick_.hasData) return;
    if (!ensureFlyoutWindow()) return;

    HDC screen = GetDC(nullptr);
    scale_ = GetDeviceCaps(screen, LOGPIXELSX) / 96.0;
    ReleaseDC(nullptr, screen);

    int w = static_cast<int>(296 * scale_);
    int h = static_cast<int>(152 * scale_);

    // Anchor above-left of the icon, then clamp to the monitor's work area so it
    // never spills under the taskbar or off-screen.
    POINT pt = {anchorX, anchorY};
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    int x = anchorX - w;
    int y = anchorY - h;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;
    if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
    if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;

    SetWindowPos(flyout_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(flyout_, nullptr, TRUE);
    UpdateWindow(flyout_);
}

void SystemTray::hideFlyout() {
    if (flyout_) ShowWindow(flyout_, SW_HIDE);
}

void SystemTray::paintFlyout(HDC hdc, const RECT& rc) {
    auto S = [&](int v) { return static_cast<int>(v * scale_); };
    const int w = rc.right, h = rc.bottom;

    // Korra palette, shared with the main window.
    const COLORREF kBg = RGB(0x1B, 0x16, 0x13);
    const COLORREF kBorder = RGB(0x5C, 0x3F, 0x29);
    const COLORREF kAccent = RGB(0xE0, 0x79, 0x42);
    const COLORREF kCream = RGB(0xE6, 0xD6, 0xC4);
    const COLORREF kMuted = RGB(0xB8, 0xA6, 0x92);

    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    // Outer border.
    HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, 0, 0, w, h);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    // Left accent bar.
    HBRUSH accent = CreateSolidBrush(kAccent);
    RECT bar = {0, 0, S(4), h};
    FillRect(hdc, &bar, accent);
    DeleteObject(accent);

    SetBkMode(hdc, TRANSPARENT);

    auto makeFont = [&](int px, bool bold) {
        return CreateFontW(S(px), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE,
                           FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    HFONT fName = makeFont(15, false);
    HFONT fBig = makeFont(34, true);
    HFONT fBody = makeFont(14, false);

    const int x = S(16);
    int y = S(10);
    const int textRight = w - S(12);

    // Location (ellipsized if long).
    SelectObject(hdc, fName);
    SetTextColor(hdc, kCream);
    RECT lr = {x, y, textRight, y + S(20)};
    DrawTextW(hdc, quick_.location.c_str(), -1, &lr,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    y += S(26);

    // Big temperature with the condition beside it.
    SelectObject(hdc, fBig);
    SetTextColor(hdc, kCream);
    SIZE tsz;
    GetTextExtentPoint32W(hdc, quick_.tempBig.c_str(),
                          static_cast<int>(quick_.tempBig.size()), &tsz);
    TextOutW(hdc, x, y, quick_.tempBig.c_str(), static_cast<int>(quick_.tempBig.size()));
    SelectObject(hdc, fBody);
    SetTextColor(hdc, kMuted);
    RECT cr = {x + tsz.cx + S(10), y + S(16), textRight, y + S(16) + S(18)};
    DrawTextW(hdc, quick_.condition.c_str(), -1, &cr,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    y += tsz.cy + S(4);

    // Supporting lines.
    SelectObject(hdc, fBody);
    SetTextColor(hdc, kCream);
    auto line = [&](const std::wstring& s) {
        if (s.empty()) return;
        TextOutW(hdc, x, y, s.c_str(), static_cast<int>(s.size()));
        y += S(19);
    };
    line(quick_.feels);
    line(quick_.hilo);
    {
        std::wstring wr = quick_.wind;
        if (!quick_.rain.empty()) wr += (wr.empty() ? L"" : L"    ") + quick_.rain;
        line(wr);
    }

    DeleteObject(fName);
    DeleteObject(fBig);
    DeleteObject(fBody);
}

LRESULT CALLBACK SystemTray::FlyoutWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;  // click-through, like a tooltip
        case WM_ERASEBKGND:
            return 1;              // painted in WM_PAINT (double-buffered)
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            auto* self = reinterpret_cast<SystemTray*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self) self->paintFlyout(mem, rc);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace wd

#endif // _WIN32
