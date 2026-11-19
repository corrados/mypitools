#include "net/adb_keys.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace irhub::adb {
namespace {

constexpr char kEventDevice[] = "/dev/input/event1";

bool copy_command(const char* command, char* out, size_t out_size) {
    if (command == nullptr || out == nullptr || out_size == 0) {
        return false;
    }
    const int written = std::snprintf(out, out_size, "%s", command);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

}  // namespace

bool is_numeric_key(const char* key) {
    if (key == nullptr || key[0] == '\0') {
        return false;
    }
    for (const char* c = key; *c != '\0'; ++c) {
        if (!std::isdigit(static_cast<unsigned char>(*c))) {
            return false;
        }
    }
    return true;
}

bool build_shell_command(const char* key, char* out, size_t out_size) {
    if (key == nullptr || out == nullptr || out_size == 0) {
        return false;
    }

    if (std::strcmp(key, "HOME") == 0) {
        return copy_command("input keyevent 3\n", out, out_size);
    }
    if (std::strcmp(key, "1") == 0) {
        return copy_command(
            "am start -n de.swr.ard.avp.mobile.android.amazon/.TvActivity\n", out, out_size);
    }
    if (std::strcmp(key, "2") == 0) {
        return copy_command("am start -n org.xbmc.kodi/.Splash\n", out, out_size);
    }
    if (std::strcmp(key, "3") == 0) {
        return copy_command(
            "am start -n org.smarttube.stable/com.liskovsoft.smartyoutubetv2.tv.ui.main.SplashActivity\n",
            out, out_size);
    }
    if (std::strcmp(key, "4") == 0) {
        return copy_command(
            "am start -n com.amazon.firetv.youtube/dev.cobalt.app.MainActivity\n", out, out_size);
    }
    if (!is_numeric_key(key)) {
        return false;
    }

    const int written = std::snprintf(out, out_size,
                                      "sendevent %s 1 %s 1 && sendevent %s 0 0 0 &&"
                                      "sendevent %s 1 %s 0 && sendevent %s 0 0 0\n",
                                      kEventDevice, key, kEventDevice, kEventDevice, key,
                                      kEventDevice);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

}  // namespace irhub::adb
