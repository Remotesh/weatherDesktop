#pragma once

#include <string>

namespace wd {

// Result of a one-shot OS geolocation request.
struct DetectedLocation {
    bool ok = false;
    double latitude = 0.0;
    double longitude = 0.0;
    std::string error;  // human-readable reason when ok == false
};

// Read the device's current location once via the OS (Windows Geolocation API).
// Blocking and permission-gated: only ever called on explicit user action, from
// a background thread. Returns ok == false (with `error` set) when the platform
// has no geolocation, permission is denied, or the request fails/times out.
DetectedLocation detectOSLocation();

} // namespace wd
