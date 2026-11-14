#include "control/state_maps.hpp"

#include <cstring>

namespace irhub::control {
namespace {

struct StateEntry {
    const char* button;
    State state;
};

struct CommandEntry {
    const char* button;
    const char* command;
};

constexpr StateEntry kStateMap[] = {
    {"1", State::Projector}, {"2", State::Tv},   {"3", State::Light},
    {"4", State::Dvd},       {"5", State::Switch}, {"POWER", State::Idle},
};

constexpr CommandEntry kProjectorMap[] = {
    {"VOL+", "BAR VOL+"},
    {"VOL-", "BAR VOL-"},
    {"MUTE", "BAR MUTE"},
    {"DISPLAY", "BAR MUTE"},
    {"R2", "BAR SURROUNDON"},
    {"R3", "BAR SURROUNDOFF"},
    {"SOURCE", "BEAM SOURCE"},
    {"R1", "BEAM OK"},
    {"UP", "FIRETVBEAM 103"},
    {"DOWN", "FIRETVBEAM 108"},
    {"LEFT", "FIRETVBEAM 105"},
    {"RIGHT", "FIRETVBEAM 106"},
    {"OK", "FIRETVBEAM 96"},
    {"RETURN", "FIRETVBEAM 158"},
    {"MENU", "FIRETVBEAM 127"},
    {"REWIND", "FIRETVBEAM 168"},
    {"PLAY", "FIRETVBEAM 164"},
    {"FORWARD", "FIRETVBEAM 208"},
    {"START", "FIRETVBEAM 746"},
    {"HOME", "FIRETVBEAM HOME"},
    {"1", "FIRETVBEAM 1"},
    {"2", "FIRETVBEAM 2"},
    {"3", "FIRETVBEAM 3"},
    {"4", "FIRETVBEAM 4"},
    {"5", "FIRETVBEAM 5"},
    {"6", "FIRETVBEAM 6"},
};

constexpr CommandEntry kTvMap[] = {
    {"VOL+", "BAR VOL+"},     {"VOL-", "BAR VOL-"},     {"MUTE", "BAR MUTE"},
    {"DISPLAY", "BAR MUTE"},  {"UP", "TV UP"},          {"DOWN", "TV DOWN"},
    {"LEFT", "TV LEFT"},      {"RIGHT", "TV RIGHT"},    {"OK", "TV OK"},
    {"1", "TV 1"},            {"2", "TV 2"},            {"3", "TV 3"},
    {"4", "TV 4"},            {"5", "TV 5"},            {"6", "TV 6"},
    {"7", "TV 7"},            {"8", "TV 8"},            {"9", "TV 9"},
    {"0", "TV 0"},            {"RETURN", "TV RETURN"},  {"MENU", "TV MENU"},
    {"CH+", "TV CH+"},        {"CH-", "TV CH-"},        {"TEXT", "TV TEXT"},
    {"LIST", "TV LIST"},      {"SOURCE", "TV SOURCE"},
};

constexpr CommandEntry kDvdMap[] = {
    {"VOL+", "BAR VOL+"},     {"VOL-", "BAR VOL-"},     {"MUTE", "BAR MUTE"},
    {"DISPLAY", "BAR MUTE"},  {"SOURCE", "BEAM SOURCE"}, {"R1", "BEAM OK"},
    {"UP", "DVD UP"},         {"DOWN", "DVD DOWN"},     {"LEFT", "DVD LEFT"},
    {"RIGHT", "DVD RIGHT"},   {"OK", "DVD OK"},         {"1", "DVD 1"},
    {"2", "DVD 2"},           {"3", "DVD 3"},           {"4", "DVD 4"},
    {"5", "DVD 5"},           {"6", "DVD 6"},           {"7", "DVD 7"},
    {"8", "DVD 8"},           {"9", "DVD 9"},           {"0", "DVD 0"},
    {"MENU", "DVD MENU"},     {"PAUSE", "DVD PAUSE"},   {"PREVIOUS", "DVD PREVIOUS"},
    {"NEXT", "DVD NEXT"},     {"RETURN", "DVD RETURN"}, {"HOME", "DVD HOME"},
    {"TEXT", "DVD POPUPMENU"}, {"LIST", "DVD OPTIONS"}, {"PLAY", "DVD PLAY"},
    {"FORWARD", "DVD FORWARD"}, {"REWIND", "DVD REWIND"}, {"STOP", "DVD STOP"},
    {"REC", "DVD EJECT"},     {"ANGLE", "DVD EJECT"},
};

constexpr CommandEntry kLightMap[] = {
    {"UP", "LED BRIGHTER"},   {"DOWN", "LED DIMMER"},   {"LEFT", "LED DIMMER"},
    {"RIGHT", "LED BRIGHTER"}, {"VOL+", "LED BRIGHTER"}, {"VOL-", "LED DIMMER"},
    {"CH+", "LED BRIGHTER"},  {"CH-", "LED DIMMER"},    {"RED", "LED RED"},
    {"GREEN", "LED GREEN"},   {"YELLOW", "LED YELLOW"}, {"BLUE", "LED BLUE"},
    {"DISPLAY", "LED RED1"},  {"LIST", "LED CYAN"},     {"MENU", "LED MAGENTA"},
    {"RETURN", "LED ORANGE"}, {"L2", "LED ORANGE1"},    {"L3", "LED GREEN1"},
    {"R1", "LED GREY"},       {"R2", "LED BLUE1"},      {"R3", "LED MAGENTA1"},
    {"OK", "LED WHITE"},      {"PLAY", "LED SMOOTH"},
};

constexpr CommandEntry kSelectMap[] = {
    {"UP", "LED BRIGHTER"},   {"DOWN", "LED DIMMER"},   {"PLAY", "LED SMOOTH"},
    {"VOL+", "BAR BASS+"},    {"VOL-", "BAR BASS-"},    {"CH+", "BAR TREB+"},
    {"CH-", "BAR TREB-"},     {"HOLD", "TV HDMI1"},     {"STOP", "TV HDMI2"},
    {"TEXT", "TV HDMI3"},     {"REC", "TV HDMI4"},      {"REWIND", "DVD AUDIO"},
    {"FORWARD", "DVD SUBTITLE"}, {"SOURCE", "BEAM SOURCE"}, {"R1", "BEAM OK"},
    {"RED", "LED RED"},       {"GREEN", "LED GREEN"},   {"BLUE", "LED BLUE"},
    {"YELLOW", "LED YELLOW"},
};

template <size_t N>
const char* find_command(const CommandEntry (&entries)[N], const char* button) {
    for (const CommandEntry& entry : entries) {
        if (std::strcmp(entry.button, button) == 0) {
            return entry.command;
        }
    }
    return nullptr;
}

}  // namespace

bool lookup_state_button(const char* button, State& state) {
    if (button == nullptr) {
        return false;
    }
    for (const StateEntry& entry : kStateMap) {
        if (std::strcmp(entry.button, button) == 0) {
            state = entry.state;
            return true;
        }
    }
    return false;
}

const char* lookup_command(MapId map, const char* button) {
    if (button == nullptr) {
        return nullptr;
    }
    switch (map) {
        case MapId::Projector:
            return find_command(kProjectorMap, button);
        case MapId::Tv:
            return find_command(kTvMap, button);
        case MapId::Light:
            return find_command(kLightMap, button);
        case MapId::Dvd:
            return find_command(kDvdMap, button);
        case MapId::None:
            return nullptr;
    }
    return nullptr;
}

const char* lookup_select_command(const char* button) {
    return button == nullptr ? nullptr : find_command(kSelectMap, button);
}

}  // namespace irhub::control
