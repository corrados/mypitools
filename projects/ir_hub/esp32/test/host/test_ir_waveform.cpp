#include <cassert>
#include <cstdint>

#include "ir/ir_codes.hpp"
#include "ir/ir_waveform.hpp"

namespace {

using irhub::ir::Device;
using irhub::ir::Waveform;

const char* code_for(Device device, const char* command) {
    const char* code = nullptr;
    assert(irhub::ir::find_code(device, command, code));
    assert(code != nullptr);
    return code;
}

void test_standard_tv_power() {
    const char* code = code_for(Device::Tv, "POWER");
    Waveform waveform;
    assert(irhub::ir::build_waveform(Device::Tv, code, false, waveform));

    assert(waveform.size() == 67);
    assert(waveform[0].mark);
    assert(waveform[0].duration_us == 9000);
    assert(!waveform[1].mark);
    assert(waveform[1].duration_us == 4500);
    assert(waveform[2].mark);
    assert(waveform[2].duration_us == 619);
    assert(!waveform[3].mark);
    assert(waveform[3].duration_us == 507);
    assert(waveform.back().mark);
    assert(waveform.back().duration_us == 619);
    assert(irhub::ir::carrier_frequency_hz(Device::Tv) == 38000);
}

void test_led_long_gap() {
    const char* code = code_for(Device::Led, "POWERON");
    Waveform waveform;
    assert(irhub::ir::build_waveform(Device::Led, code, false, waveform));

    assert(waveform.size() == 68);
    assert(!waveform.back().mark);
    assert(waveform.back().duration_us == 110000);
}

void test_beam_trailing_gap() {
    const char* code = code_for(Device::Beam, "POWER");
    Waveform waveform;
    assert(irhub::ir::build_waveform(Device::Beam, code, false, waveform));

    assert(waveform.size() == 68);
    assert(!waveform.back().mark);
    assert(waveform.back().duration_us == 10000);
}

void test_dvd_no_trailing_mark() {
    const char* code = code_for(Device::Dvd, "POWER");
    Waveform waveform;
    assert(irhub::ir::build_waveform(Device::Dvd, code, false, waveform));

    assert(waveform.size() == 43);
    assert(waveform[0].mark);
    assert(waveform[0].duration_us == 2448);
    assert(!waveform.back().mark);
    assert(waveform.back().duration_us == 14400);
}

void test_rc6_toggle() {
    const char* code = code_for(Device::Bar, "POWER");

    Waveform toggle_zero;
    assert(irhub::ir::build_waveform(Device::Bar, code, false, toggle_zero));
    assert(toggle_zero.size() == 45);
    assert(toggle_zero[0].mark);
    assert(toggle_zero[0].duration_us == 2664);
    assert(!toggle_zero[1].mark);
    assert(toggle_zero[1].duration_us == 888);
    assert(toggle_zero[2].mark);
    assert(toggle_zero[2].duration_us == 444);
    assert(!toggle_zero[3].mark);
    assert(toggle_zero[3].duration_us == 444);
    assert(!toggle_zero[10].mark);
    assert(toggle_zero[10].duration_us == 888);
    assert(toggle_zero[11].mark);
    assert(toggle_zero[11].duration_us == 888);
    assert(!toggle_zero.back().mark);
    assert(toggle_zero.back().duration_us == 2664);
    assert(irhub::ir::carrier_frequency_hz(Device::Bar) == 36000);

    Waveform toggle_one;
    assert(irhub::ir::build_waveform(Device::Bar, code, true, toggle_one));
    assert(toggle_one.size() == toggle_zero.size());
    assert(toggle_one[10].mark);
    assert(toggle_one[10].duration_us == 888);
    assert(!toggle_one[11].mark);
    assert(toggle_one[11].duration_us == 888);
}

void test_rc6_software_carrier() {
    const char* code = code_for(Device::Bar, "POWER");
    Waveform waveform;
    assert(irhub::ir::build_bar_carrier_waveform(code, false, waveform));

    assert(waveform.size() == 919);
    for (size_t i = 0; i < 192; i += 2) {
        assert(waveform[i].mark);
        assert(waveform[i].duration_us == 14);
        assert(!waveform[i + 1].mark);
        assert(waveform[i + 1].duration_us == 14);
    }
    assert(!waveform.back().mark);
    assert(waveform.back().duration_us == 2664);
}

void test_rc6_irremote_waveform() {
    const char* code = code_for(Device::Bar, "POWER");
    Waveform waveform;
    assert(irhub::ir::build_bar_irremote_waveform(code, false, waveform));

    assert(waveform.size() == 919);
    assert(waveform[0].mark);
    assert(waveform[0].duration_us == 9);
    assert(!waveform[1].mark);
    assert(waveform[1].duration_us == 19);
    assert(!waveform.back().mark);
    assert(waveform.back().duration_us == 187 * 444);
}

void test_unknown_entries() {
    const char* code = nullptr;
    assert(!irhub::ir::find_code(Device::Tv, "DOESNOTEXIST", code));
    assert(!irhub::ir::find_code(Device::FireTvBeam, "POWER", code));

    Waveform waveform;
    assert(!irhub::ir::build_waveform(Device::Unknown, "0", false, waveform));
    assert(waveform.empty());
    assert(irhub::ir::carrier_frequency_hz(Device::Unknown) == 0);
}

}  // namespace

int main() {
    test_standard_tv_power();
    test_led_long_gap();
    test_beam_trailing_gap();
    test_dvd_no_trailing_mark();
    test_rc6_toggle();
    test_rc6_software_carrier();
    test_rc6_irremote_waveform();
    test_unknown_entries();
    return 0;
}
