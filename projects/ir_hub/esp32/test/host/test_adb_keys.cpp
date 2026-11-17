#include <cassert>
#include <cstring>

#include "net/adb_keys.hpp"

namespace {

bool equals(const char* expected, const char* actual) {
    return std::strcmp(expected, actual) == 0;
}

void test_special_commands() {
    char out[192] = {};

    assert(irhub::adb::build_shell_command("HOME", out, sizeof(out)));
    assert(equals("input keyevent 3\n", out));

    assert(irhub::adb::build_shell_command("1", out, sizeof(out)));
    assert(equals("am start -n de.swr.ard.avp.mobile.android.amazon/.TvActivity\n", out));

    assert(irhub::adb::build_shell_command("2", out, sizeof(out)));
    assert(equals("am start -n org.xbmc.kodi/.Splash\n", out));

    assert(irhub::adb::build_shell_command("3", out, sizeof(out)));
    assert(equals(
        "am start -n org.smarttube.stable/com.liskovsoft.smartyoutubetv2.tv.ui.main.SplashActivity\n",
        out));

    assert(irhub::adb::build_shell_command("4", out, sizeof(out)));
    assert(equals("am start -n com.amazon.firetv.youtube/dev.cobalt.app.MainActivity\n", out));
}

void test_numeric_command() {
    char out[192] = {};
    assert(irhub::adb::build_shell_command("103", out, sizeof(out)));
    assert(equals("sendevent /dev/input/event1 1 103 1 && sendevent /dev/input/event1 0 0 0 &&"
                  "sendevent /dev/input/event1 1 103 0 && sendevent /dev/input/event1 0 0 0\n",
                  out));
}

void test_invalid_commands() {
    char out[192] = {};
    assert(!irhub::adb::build_shell_command("", out, sizeof(out)));
    assert(!irhub::adb::build_shell_command("HOME ", out, sizeof(out)));
    assert(!irhub::adb::build_shell_command("103;rm", out, sizeof(out)));
    assert(!irhub::adb::build_shell_command("103", out, 16));
    assert(!irhub::adb::is_numeric_key("1a"));
    assert(irhub::adb::is_numeric_key("746"));
}

}  // namespace

int main() {
    test_special_commands();
    test_numeric_command();
    test_invalid_commands();
    return 0;
}
