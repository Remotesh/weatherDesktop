#pragma once

#include <optional>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace wd {

class SystemTray {
public:
    enum class MenuAction { Show, Refresh, Quit };

#ifdef _WIN32
    bool initialize(HWND hwnd, HICON icon);
    void shutdown();

    void setTooltip(const std::wstring& text);
    void showBalloon(const std::wstring& title, const std::wstring& msg);

    bool handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    std::optional<MenuAction> pollAction();

    static constexpr UINT WM_TRAYICON = WM_APP + 1;

private:
    void showContextMenu();

    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW nid_ = {};
    bool initialized_ = false;
    std::optional<MenuAction> pendingAction_;

#else // Linux stubs

    bool initialize() { return true; }
    void shutdown() {}

    void setTooltip(const std::string&) {}
    void showBalloon(const std::string&, const std::string&) {}

    std::optional<MenuAction> pollAction() { return std::nullopt; }

#endif
};

} // namespace wd
