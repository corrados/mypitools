#include "control/state_machine.hpp"

#include <cstring>

namespace irhub::control {
namespace {

bool add(Outcome& out, const Action& action) {
    if (out.action_count >= kMaxActions) {
        return false;
    }
    out.actions[out.action_count++] = action;
    return true;
}

void add_ir_device(Outcome& out, ir::Device device, const char* command, uint8_t repeat) {
    Action action = {};
    action.kind = ActionKind::Ir;
    action.device = device;
    action.command = command;
    action.repeat = repeat;
    add(out, action);
}

bool parse_ir_name(const char* full, ir::Device& device, const char*& command) {
    if (full == nullptr) {
        return false;
    }

    struct Prefix {
        const char* text;
        size_t length;
        ir::Device device;
    };
    constexpr Prefix kPrefixes[] = {
        {"BAR ", 4, ir::Device::Bar},
        {"BEAM ", 5, ir::Device::Beam},
        {"LED ", 4, ir::Device::Led},
        {"TV ", 3, ir::Device::Tv},
        {"DVD ", 4, ir::Device::Dvd},
        {"FIRETVBEAM ", 11, ir::Device::FireTvBeam},
    };

    for (const Prefix& prefix : kPrefixes) {
        if (std::strncmp(full, prefix.text, prefix.length) == 0 && full[prefix.length] != '\0') {
            device = prefix.device;
            command = full + prefix.length;
            return true;
        }
    }
    return false;
}

void add_ir_name(Outcome& out, const char* full, uint8_t repeat) {
    ir::Device device = ir::Device::Unknown;
    const char* command = nullptr;
    if (parse_ir_name(full, device, command)) {
        add_ir_device(out, device, command, repeat);
    }
}

void add_delay(Outcome& out, uint16_t delay_ms) {
    Action action = {};
    action.kind = ActionKind::DelayMs;
    action.delay_ms = delay_ms;
    add(out, action);
}

void add_sequence(Outcome& out, SequenceId sequence, State state = State::Idle,
                  const char* argument = nullptr) {
    Action action = {};
    action.kind = ActionKind::StartSequence;
    action.sequence = sequence;
    action.sequence_state = state;
    action.argument = argument;
    add(out, action);
}

void add_simple(Outcome& out, ActionKind kind) {
    Action action = {};
    action.kind = kind;
    add(out, action);
}

void add_branch_actions(Outcome& out, State old_state, State target) {
    switch (target) {
        case State::Idle:
            out.next.mapping = MapId::None;
            out.next.alt_func = true;
            out.next.led_is_on = false;

            add_ir_name(out, "TV POWEROFF", 2);
            if (old_state == State::Tv || old_state == State::Switch) {
                add_delay(out, 3000);
            }
            if (old_state == State::Projector || old_state == State::Dvd ||
                old_state == State::Tv || old_state == State::Switch) {
                add_ir_name(out, "BAR POWER", 2);
            }
            if (old_state == State::Projector || old_state == State::Dvd) {
                add_sequence(out, SequenceId::ProjectorOff);
            }
            add_ir_name(out, "DVD POWEROFF", 2);
            add_ir_name(out, "LED POWEROFF", 2);
            add_simple(out, ActionKind::ResetRadioOffTimer);
            break;

        case State::Projector:
            out.next.mapping = MapId::Projector;
            out.next.alt_func = false;
            out.next.led_is_on = false;

            add_ir_name(out, "TV POWEROFF", 2);
            if (old_state == State::Tv || old_state == State::Switch) {
                add_delay(out, 3000);
            }
            add_sequence(out, SequenceId::BarOn, State::Idle, "BLUETOOTH");
            if (old_state != State::Dvd) {
                add_sequence(out, SequenceId::ProjectorOnWithInput, State::Projector, "HDMI1");
            }
            add_ir_name(out, "LED POWEROFF", 2);
            add_ir_name(out, "DVD POWEROFF", 2);
            break;

        case State::Tv:
        case State::Switch:
            out.next.mapping = MapId::Tv;
            out.next.alt_func = false;
            out.next.led_is_on = true;

            add_ir_name(out, "LED POWEROFF", 2);
            if (old_state == State::Projector || old_state == State::Dvd) {
                add_sequence(out, SequenceId::ProjectorOff);
                add_delay(out, 3000);
            }
            add_ir_name(out, "DVD POWEROFF", 2);
            if (target == State::Tv) {
                if (old_state == State::Switch) {
                    add_ir_name(out, "TV CH+", 1);
                } else {
                    add_sequence(out, SequenceId::TvOn, State::Tv, "CH+");
                }
            } else {
                if (old_state == State::Tv) {
                    add_ir_name(out, "TV HDMI2", 2);
                } else {
                    add_sequence(out, SequenceId::TvOn, State::Switch, "HDMI2");
                }
            }
            add_sequence(out, SequenceId::BarOn, State::Idle, "OPTICAL");
            break;

        case State::Light:
            out.next.mapping = MapId::Light;
            out.next.alt_func = false;
            out.next.led_is_on = true;

            add_ir_name(out, "TV POWEROFF", 2);
            if (old_state == State::Tv || old_state == State::Switch) {
                add_delay(out, 3000);
            }
            if (old_state == State::Projector || old_state == State::Dvd ||
                old_state == State::Tv || old_state == State::Switch) {
                add_ir_name(out, "BAR POWER", 2);
            }
            add_ir_name(out, "LED POWERON", 2);
            if (old_state == State::Projector || old_state == State::Dvd) {
                add_sequence(out, SequenceId::ProjectorOff);
            }
            add_ir_name(out, "DVD POWEROFF", 2);
            add_ir_name(out, "LED WHITE", 2);
            add_sequence(out, SequenceId::LedMaxBrightness);
            break;

        case State::Dvd:
            out.next.mapping = MapId::Dvd;
            out.next.alt_func = false;
            out.next.led_is_on = false;

            add_ir_name(out, "TV POWEROFF", 2);
            if (old_state == State::Tv || old_state == State::Switch) {
                add_delay(out, 3000);
            }
            add_ir_name(out, "DVD POWERON", 2);
            add_sequence(out, SequenceId::BarOn, State::Idle, "BLUETOOTH");
            if (old_state != State::Projector) {
                add_sequence(out, SequenceId::ProjectorOnWithInput, State::Dvd, "HDMI2");
            }
            add_ir_name(out, "LED POWEROFF", 2);
            break;
    }
}

}  // namespace

MachineState initial_machine_state() {
    return {State::Idle, State::Idle, true, false, MapId::None};
}

Outcome process(const MachineState& current, const char* raw_button) {
    Outcome out = {};
    out.next = current;
    out.action_count = 0;
    out.transitioned = false;

    const char* button = raw_button == nullptr ? "" : raw_button;

    if (std::strcmp(button, "SELECT") == 0 && current.state != State::Idle) {
        out.next.alt_func = !out.next.alt_func;
    }

    if (std::strcmp(button, "LIGHT") == 0) {
        if (current.state == State::Idle) {
            button = "3";
        } else if (current.state == State::Light) {
            button = "POWER";
        } else {
            add_ir_name(out, out.next.led_is_on ? "LED POWEROFF" : "LED POWERON", 1);
            out.next.led_is_on = !out.next.led_is_on;
        }
    }

    State target = State::Idle;
    const bool is_state_button = lookup_state_button(button, target);

    if ((current.state == State::Light || current.state == State::Switch) && is_state_button) {
        out.next.alt_func = true;
    }

    if ((out.next.alt_func || std::strcmp(button, "POWER") == 0) && is_state_button) {
        out.transitioned = true;

        if (current.state == State::Idle && target != State::Idle) {
            add_simple(out, ActionKind::RadioPowerOn);
            add_simple(out, ActionKind::ColdStartCheck);
        }

        State old_state = current.state;
        if (old_state == target) {
            old_state = current.prev_state;
        }

        out.next.alt_func = false;
        add_branch_actions(out, old_state, target);
        out.next.prev_state = old_state;
        out.next.state = target;
        return out;
    }

    const char* full = nullptr;
    if (out.next.alt_func) {
        full = lookup_select_command(button);
    } else {
        full = lookup_command(out.next.mapping, button);
    }
    if (full != nullptr) {
        add_ir_name(out, full, 1);
    }

    return out;
}

}  // namespace irhub::control
