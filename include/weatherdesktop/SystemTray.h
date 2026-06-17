#pragma once

#include <optional>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace wd {

#ifdef _WIN32
// Compact weather snapshot painted in the on-hover tray flyout. All strings are
// pre-formatted (unit-aware) by the app so the tray code stays presentation-only.
struct TrayQuickLook {
    std::wstring location;
    std::wstring tempBig;     // e.g. "64°F"
    std::wstring condition;   // e.g. "Cloudy"
    std::wstring feels;       // e.g. "Feels like 58°"
    std::wstring hilo;        // e.g. "High 65°   Low 52°"
    std::wstring wind;        // e.g. "Wind 12 mph N"
    std::wstring rain;        // e.g. "Rain 18%"
    bool hasData = false;
};
#endif

class SystemTray {
public:
    enum class MenuAction { Show, Refresh, Quit };

#ifdef _WIN32
    bool initialize(HWND hwnd, HICON icon);
    void shutdown();

    void setTooltip(const std::wstring& text);
    void setQuickLook(const TrayQuickLook& q);
    void showBalloon(const std::wstring& title, const std::wstring& msg);

    bool handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    std::optional<MenuAction> pollAction();

    static constexpr UINT WM_TRAYICON = WM_APP + 1;

private:
    void showContextMenu();

    // On-hover flyout (a small borderless GDI window, independent of the main
    // OpenGL window so it works while minimized to tray).
    bool ensureFlyoutWindow();
    void showFlyout(int anchorX, int anchorY);
    void hideFlyout();
    void paintFlyout(HDC hdc, const RECT& rc);
    static LRESULT CALLBACK FlyoutWndProc(HWND, UINT, WPARAM, LPARAM);

    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW nid_ = {};
    bool initialized_ = false;
    std::optional<MenuAction> pendingAction_;

    HWND flyout_ = nullptr;
    TrayQuickLook quick_;
    double scale_ = 1.0;  // DPI scale captured when the flyout is shown

#else // Linux stubs

    bool initialize() { return true; }
    void shutdown() {}

    void setTooltip(const std::string&) {}
    void showBalloon(const std::string&, const std::string&) {}

    std::optional<MenuAction> pollAction() { return std::nullopt; }

#endif
};

} // namespace wd
