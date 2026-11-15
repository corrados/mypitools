#pragma once

#include <cstddef>
#include <cstdint>

#include "control/state_maps.hpp"
#include "control/state_preview.hpp"
#include "ir/ir_types.hpp"

namespace irhub::control {

enum class SequenceId : uint8_t {
    TvOn,
    ProjectorOnWithInput,
    ProjectorOff,
    BarOn,
    LedMaxBrightness,
};

enum class ActionKind : uint8_t {
    Ir,
    DelayMs,
    RadioPowerOn,
    ColdStartCheck,
    ResetRadioOffTimer,
    StartSequence,
};

struct Action {
    ActionKind kind;
    ir::Device device;
    uint8_t repeat;
    uint16_t delay_ms;
    SequenceId sequence;
    const char* command;
    const char* argument;
    State sequence_state;
};

constexpr size_t kMaxActions = 16;

struct MachineState {
    State state;
    State prev_state;
    bool alt_func;
    bool led_is_on;
    MapId mapping;
};

struct Outcome {
    MachineState next;
    bool transitioned;
    bool help_action;
    size_t action_count;
    Action actions[kMaxActions];
};

MachineState initial_machine_state();
Outcome process(const MachineState& current, const char* button);

}  // namespace irhub::control
