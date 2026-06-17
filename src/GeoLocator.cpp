#include "weatherdesktop/GeoLocator.h"

// Native OS geolocation is only wired up for Windows, and only when the WinRT
// ABI headers are present. Note C++/WinRT ("winrt/...") does not build under
// MinGW, so this uses the plain WinRT ABI + WRL ComPtr, which does.
#if defined(_WIN32) && __has_include(<windows.devices.geolocation.h>) && \
    __has_include(<wrl/client.h>)
#define WD_HAS_WINRT_GEO 1
#endif

#ifdef WD_HAS_WINRT_GEO

#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>
#include <windows.foundation.h>
#include <windows.devices.geolocation.h>
#include <cwchar>

namespace wd {

using namespace ABI::Windows::Devices::Geolocation;
using namespace ABI::Windows::Foundation;
using Microsoft::WRL::ComPtr;

DetectedLocation detectOSLocation() {
    DetectedLocation out;

    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    // RPC_E_CHANGED_MODE => COM is already up in another mode on this thread;
    // that's fine, we just shouldn't balance it with our own RoUninitialize.
    bool inited = SUCCEEDED(hr);
    auto cleanup = [&]() { if (inited) RoUninitialize(); };

    const wchar_t* clsName = L"Windows.Devices.Geolocation.Geolocator";
    HSTRING cls = nullptr;
    if (FAILED(WindowsCreateString(clsName,
                                   static_cast<UINT32>(wcslen(clsName)), &cls))) {
        out.error = "OS location unavailable";
        cleanup();
        return out;
    }

    ComPtr<IInspectable> insp;
    hr = RoActivateInstance(cls, &insp);
    WindowsDeleteString(cls);
    if (FAILED(hr) || !insp) {
        out.error = "OS location unavailable";
        cleanup();
        return out;
    }

    ComPtr<IGeolocator> geo;
    if (FAILED(insp.As(&geo)) || !geo) {
        out.error = "OS location unavailable";
        cleanup();
        return out;
    }

    ComPtr<IAsyncOperation<Geoposition*>> op;
    hr = geo->GetGeopositionAsync(&op);
    if (FAILED(hr) || !op) {
        out.error = "Location permission denied or unavailable";
        cleanup();
        return out;
    }

    // Bridge the async call to this blocking worker thread by polling its status
    // (no completion-handler class needed). Cap the wait so a hung sensor or a
    // pending permission prompt can't block the worker forever.
    ComPtr<IAsyncInfo> info;
    op.As(&info);
    AsyncStatus status = AsyncStatus::Started;
    for (int i = 0; i < 200 && info; ++i) {  // ~10s max
        if (FAILED(info->get_Status(&status))) break;
        if (status != AsyncStatus::Started) break;
        Sleep(50);
    }
    if (status != AsyncStatus::Completed) {
        out.error = "Location request timed out or was denied";
        cleanup();
        return out;
    }

    ComPtr<IGeoposition> pos;
    if (FAILED(op->GetResults(&pos)) || !pos) {
        out.error = "Couldn't read location";
        cleanup();
        return out;
    }
    ComPtr<IGeocoordinate> coord;
    if (FAILED(pos->get_Coordinate(&coord)) || !coord) {
        out.error = "Couldn't read coordinates";
        cleanup();
        return out;
    }

    DOUBLE lat = 0.0, lon = 0.0;
    coord->get_Latitude(&lat);
    coord->get_Longitude(&lon);
    out.ok = true;
    out.latitude = lat;
    out.longitude = lon;

    cleanup();
    return out;
}

} // namespace wd

#else  // no native geolocation on this platform/toolchain

namespace wd {
DetectedLocation detectOSLocation() {
    DetectedLocation out;
    out.error = "OS location isn't available on this build";
    return out;
}
} // namespace wd

#endif
