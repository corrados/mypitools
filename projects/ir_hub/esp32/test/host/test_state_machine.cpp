#include <cassert>
#include <cstdio>
#include <cstring>

#include "control/state_machine.hpp"
#include "control/state_maps.hpp"

namespace {

using irhub::control::Action;
using irhub::control::ActionKind;
using irhub::control::MachineState;
using irhub::control::MapId;
using irhub::control::Outcome;
using irhub::control::SequenceId;
using irhub::control::State;
using irhub::ir::Device;

void test_state_map() {
    State state = State::Idle;
    assert(irhub::control::lookup_state_button("1", state) && state == State::Projector);
    assert(irhub::control::lookup_state_button("2", state) && state == State::Tv);
    assert(irhub::control::lookup_state_button("3", state) && state == State::Light);
    assert(irhub::control::lookup_state_button("4", state) && state == State::Dvd);
    assert(irhub::control::lookup_state_button("5", state) && state == State::Switch);
    assert(irhub::control::lookup_state_button("POWER", state) && state == State::Idle);
    assert(!irhub::control::lookup_state_button("VOL+", state));
    assert(!irhub::control::lookup_state_button(nullptr, state));
}

void test_command_maps() {
    assert(std::strcmp(irhub::control::lookup_command(MapId::Tv, "VOL+"), "BAR VOL+") == 0);
    assert(std::strcmp(irhub::control::lookup_command(MapId::Tv, "DISPLAY"), "BAR MUTE") == 0);
    assert(std::strcmp(irhub::control::lookup_command(MapId::Dvd, "SOURCE"), "BEAM SOURCE") == 0);
    assert(std::strcmp(irhub::control::lookup_command(MapId::Dvd, "R1"), "BEAM OK") == 0);
    assert(std::strcmp(irhub::control::lookup_command(MapId::Projector, "R2"),
                       "BAR SURROUNDON") == 0);
    assert(std::strcmp(irhub::control::lookup_command(MapId::Projector, "UP"),
                       "FIRETVBEAM 103") == 0);
    assert(std::strcmp(irhub::control::lookup_command(MapId::Light, "PLAY"),
                       "LED SMOOTH") == 0);
    assert(std::strcmp(irhub::control::lookup_command(MapId::Light, "DISPLAY"),
                       "LED RED1") == 0);
    assert(irhub::control::lookup_command(MapId::None, "VOL+") == nullptr);
    assert(irhub::control::lookup_command(MapId::Tv, "NOT_A_KEY") == nullptr);

    assert(std::strcmp(irhub::control::lookup_select_command("HOLD"), "TV HDMI1") == 0);
    assert(std::strcmp(irhub::control::lookup_select_command("STOP"), "TV HDMI2") == 0);
    assert(std::strcmp(irhub::control::lookup_select_command("VOL+"), "BAR BASS+") == 0);
    assert(irhub::control::lookup_select_command("NOT_A_KEY") == nullptr);
}

void test_initial_state() {
    const MachineState initial = irhub::control::initial_machine_state();
    assert(initial.state == State::Idle);
    assert(initial.prev_state == State::Idle);
    assert(initial.alt_func);
    assert(!initial.led_is_on);
    assert(initial.mapping == MapId::None);
}

void test_normal_mapping() {
    MachineState tv = irhub::control::initial_machine_state();
    tv.state = State::Tv;
    tv.prev_state = State::Idle;
    tv.alt_func = false;
    tv.led_is_on = true;
    tv.mapping = MapId::Tv;

    Outcome out = irhub::control::process(tv, "VOL+");
    assert(!out.transitioned);
    assert(out.next.state == State::Tv);
    assert(out.action_count == 1);
    assert(out.actions[0].kind == ActionKind::Ir);
    assert(out.actions[0].device == Device::Bar);
    assert(std::strcmp(out.actions[0].command, "VOL+") == 0);
    assert(out.actions[0].repeat == 1);

    tv.alt_func = true;
    out = irhub::control::process(tv, "VOL+");
    assert(!out.transitioned);
    assert(out.action_count == 1);
    assert(out.actions[0].device == Device::Bar);
    assert(std::strcmp(out.actions[0].command, "BASS+") == 0);
    assert(out.actions[0].repeat == 1);

    tv.alt_func = false;
    out = irhub::control::process(tv, "NOT_A_KEY");
    assert(!out.transitioned);
    assert(out.action_count == 0);
}

void test_select_and_light() {
    MachineState tv = irhub::control::initial_machine_state();
    tv.state = State::Tv;
    tv.prev_state = State::Idle;
    tv.alt_func = false;
    tv.mapping = MapId::Tv;

    Outcome out = irhub::control::process(tv, "SELECT");
    assert(!out.transitioned);
    assert(out.next.alt_func);

    out = irhub::control::process(tv, "LIGHT");
    assert(!out.transitioned);
    assert(out.next.led_is_on);
    assert(out.action_count == 1);
    assert(out.actions[0].device == Device::Led);
    assert(std::strcmp(out.actions[0].command, "POWERON") == 0);
    assert(out.actions[0].repeat == 1);

    tv.led_is_on = true;
    out = irhub::control::process(tv, "LIGHT");
    assert(!out.next.led_is_on);
    assert(std::strcmp(out.actions[0].command, "POWEROFF") == 0);

    MachineState idle = irhub::control::initial_machine_state();
    out = irhub::control::process(idle, "LIGHT");
    assert(out.transitioned);
    assert(out.next.state == State::Light);
}

void test_cold_start_actions() {
    const MachineState idle = irhub::control::initial_machine_state();
    const Outcome out = irhub::control::process(idle, "1");
    assert(out.transitioned);
    assert(out.action_count >= 4);
    assert(out.actions[0].kind == ActionKind::RadioPowerOn);
    assert(out.actions[1].kind == ActionKind::ColdStartCheck);
    assert(out.actions[2].kind == ActionKind::Ir);
    assert(out.actions[2].device == Device::Tv);
    assert(std::strcmp(out.actions[2].command, "POWEROFF") == 0);
    assert(out.actions[2].repeat == 2);
    assert(out.next.state == State::Projector);
    assert(out.next.mapping == MapId::Projector);
    assert(!out.next.alt_func);
    assert(!out.next.led_is_on);
}

void test_projector_from_idle() {
    const MachineState idle = irhub::control::initial_machine_state();
    const Outcome out = irhub::control::process(idle, "1");

    bool bar = false;
    bool projector = false;
    bool led_off = false;
    bool dvd_off = false;
    for (size_t i = 0; i < out.action_count; ++i) {
        const Action& action = out.actions[i];
        if (action.kind == ActionKind::StartSequence && action.sequence == SequenceId::BarOn) {
            bar = true;
            assert(std::strcmp(action.argument, "BLUETOOTH") == 0);
        }
        if (action.kind == ActionKind::StartSequence &&
            action.sequence == SequenceId::ProjectorOnWithInput) {
            projector = true;
            assert(action.sequence_state == State::Projector);
        }
        if (action.kind == ActionKind::Ir && action.device == Device::Led &&
            std::strcmp(action.command, "POWEROFF") == 0) {
            led_off = true;
        }
        if (action.kind == ActionKind::Ir && action.device == Device::Dvd &&
            std::strcmp(action.command, "POWEROFF") == 0) {
            dvd_off = true;
        }
    }
    assert(bar && projector && led_off && dvd_off);
}

void test_tv_from_projector() {
    MachineState projector = irhub::control::initial_machine_state();
    projector.state = State::Projector;
    projector.prev_state = State::Idle;
    projector.alt_func = true;
    projector.mapping = MapId::Projector;

    const Outcome out = irhub::control::process(projector, "2");
    assert(out.transitioned);
    assert(out.next.state == State::Tv);
    assert(out.next.mapping == MapId::Tv);
    assert(out.next.led_is_on);

    assert(out.actions[0].kind == ActionKind::Ir);
    assert(out.actions[0].device == Device::Led);
    assert(std::strcmp(out.actions[0].command, "POWEROFF") == 0);

    bool projector_off = false;
    bool tv_on = false;
    bool bar_on = false;
    bool delay = false;
    for (size_t i = 0; i < out.action_count; ++i) {
        const Action& action = out.actions[i];
        if (action.kind == ActionKind::StartSequence &&
            action.sequence == SequenceId::ProjectorOff) {
            projector_off = true;
        }
        if (action.kind == ActionKind::DelayMs && action.delay_ms == 3000) {
            delay = true;
        }
        if (action.kind == ActionKind::StartSequence && action.sequence == SequenceId::TvOn) {
            tv_on = true;
            assert(action.sequence_state == State::Tv);
            assert(std::strcmp(action.argument, "CH+") == 0);
        }
        if (action.kind == ActionKind::StartSequence && action.sequence == SequenceId::BarOn) {
            bar_on = true;
            assert(std::strcmp(action.argument, "OPTICAL") == 0);
        }
    }
    assert(projector_off && delay && tv_on && bar_on);
}

void test_light_from_tv() {
    MachineState tv = irhub::control::initial_machine_state();
    tv.state = State::Tv;
    tv.prev_state = State::Projector;
    tv.alt_func = true;
    tv.mapping = MapId::Tv;

    const Outcome out = irhub::control::process(tv, "3");
    assert(out.transitioned);
    assert(out.next.state == State::Light);
    assert(out.next.mapping == MapId::Light);

    bool delay = false;
    bool bar_power = false;
    bool led_on = false;
    bool led_white = false;
    bool led_max = false;
    for (size_t i = 0; i < out.action_count; ++i) {
        const Action& action = out.actions[i];
        if (action.kind == ActionKind::DelayMs && action.delay_ms == 3000) {
            delay = true;
        }
        if (action.kind == ActionKind::Ir && action.device == Device::Bar &&
            std::strcmp(action.command, "POWER") == 0) {
            bar_power = true;
        }
        if (action.kind == ActionKind::Ir && action.device == Device::Led &&
            std::strcmp(action.command, "POWERON") == 0) {
            led_on = true;
        }
        if (action.kind == ActionKind::Ir && action.device == Device::Led &&
            std::strcmp(action.command, "WHITE") == 0) {
            led_white = true;
        }
        if (action.kind == ActionKind::StartSequence &&
            action.sequence == SequenceId::LedMaxBrightness) {
            led_max = true;
        }
    }
    assert(delay && bar_power && led_on && led_white && led_max);
}

void test_power_from_tv() {
    MachineState tv = irhub::control::initial_machine_state();
    tv.state = State::Tv;
    tv.prev_state = State::Idle;
    tv.alt_func = false;
    tv.led_is_on = true;
    tv.mapping = MapId::Tv;

    const Outcome out = irhub::control::process(tv, "POWER");
    assert(out.transitioned);
    assert(out.next.state == State::Idle);
    assert(out.next.mapping == MapId::None);
    assert(out.next.alt_func);
    assert(!out.next.led_is_on);

    bool tv_off = false;
    bool delay = false;
    bool bar_power = false;
    bool reset = false;
    for (size_t i = 0; i < out.action_count; ++i) {
        const Action& action = out.actions[i];
        if (action.kind == ActionKind::Ir && action.device == Device::Tv &&
            std::strcmp(action.command, "POWEROFF") == 0) {
            tv_off = true;
        }
        if (action.kind == ActionKind::DelayMs && action.delay_ms == 3000) {
            delay = true;
        }
        if (action.kind == ActionKind::Ir && action.device == Device::Bar &&
            std::strcmp(action.command, "POWER") == 0) {
            bar_power = true;
        }
        if (action.kind == ActionKind::ResetRadioOffTimer) {
            reset = true;
        }
    }
    assert(tv_off && delay && bar_power && reset);
}

void test_help_action() {
    MachineState tv = irhub::control::initial_machine_state();
    tv.state = State::Tv;
    tv.prev_state = State::Idle;
    tv.alt_func = true;
    tv.mapping = MapId::Tv;

    const Outcome out = irhub::control::process(tv, "2");
    assert(out.transitioned);
    assert(out.next.prev_state == State::Idle);
    assert(out.next.state == State::Tv);

    bool tv_on = false;
    for (size_t i = 0; i < out.action_count; ++i) {
        assert(out.actions[i].kind != ActionKind::ColdStartCheck);
        if (out.actions[i].kind == ActionKind::StartSequence &&
            out.actions[i].sequence == SequenceId::TvOn) {
            tv_on = true;
        }
    }
    assert(tv_on);
}

}  // namespace

int main() {
    test_state_map();
    test_command_maps();
    test_initial_state();
    test_normal_mapping();
    test_select_and_light();
    test_cold_start_actions();
    test_projector_from_idle();
    test_tv_from_projector();
    test_light_from_tv();
    test_power_from_tv();
    test_help_action();
    std::puts("state_machine tests passed");
    return 0;
}
