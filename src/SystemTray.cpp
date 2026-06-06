#include "weatherdesktop/SystemTray.h"

#ifdef _WIN32

namespace wd {

enum TrayMenuID {
    ID_TRAY_SHOW = 1001,
    ID_TRAY_REFRESH,
    ID_TRAY_QUIT
};

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
    if (!initialized_) return;
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    initialized_ = false;
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
            case WM_LBUTTONDBLCLK:
                pendingAction_ = MenuAction::Show;
                return true;
            case WM_RBUTTONUP:
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

} // namespace wd

#endif // _WIN32
