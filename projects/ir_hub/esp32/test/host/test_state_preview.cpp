#include <cassert>
#include <cstdio>
#include <cstring>

#include "control/state_preview.hpp"

namespace {

using irhub::control::State;
using irhub::control::display_color;
using irhub::control::state_color;
using irhub::control::state_for_button_preview;
using irhub::control::state_name;

void test_state_names() {
    assert(std::strcmp(state_name(State::Idle), "IDLE") == 0);
    assert(std::strcmp(state_name(State::Projector), "PROJECTOR") == 0);
    assert(std::strcmp(state_name(State::Tv), "TV") == 0);
    assert(std::strcmp(state_name(State::Light), "LIGHT") == 0);
    assert(std::strcmp(state_name(State::Dvd), "DVD") == 0);
    assert(std::strcmp(state_name(State::Switch), "SWITCH") == 0);
}

void test_number_transitions() {
    assert(state_for_button_preview(State::Idle, "1") == State::Projector);
    assert(state_for_button_preview(State::Idle, "2") == State::Tv);
    assert(state_for_button_preview(State::Idle, "3") == State::Light);
    assert(state_for_button_preview(State::Idle, "4") == State::Dvd);
    assert(state_for_button_preview(State::Idle, "5") == State::Switch);
    assert(state_for_button_preview(State::Tv, "POWER") == State::Idle);
}

void test_light_and_unknown() {
    assert(state_for_button_preview(State::Idle, "LIGHT") == State::Light);
    assert(state_for_button_preview(State::Light, "LIGHT") == State::Idle);
    assert(state_for_button_preview(State::Tv, "LIGHT") == State::Tv);
    assert(state_for_button_preview(State::Dvd, "VOL+") == State::Dvd);
    assert(state_for_button_preview(State::Dvd, nullptr) == State::Dvd);
}

void test_colors() {
    const auto projector = state_color(State::Projector, 10);
    assert(projector.r == 0 && projector.g == 0 && projector.b == 10);

    const auto tv = state_color(State::Tv, 10);
    assert(tv.r == 0 && tv.g == 10 && tv.b == 0);

    const auto light = state_color(State::Light, 10);
    assert(light.r == 10 && light.g == 10 && light.b == 10);

    const auto dvd = state_color(State::Dvd, 10);
    assert(dvd.r == 10 && dvd.g == 10 && dvd.b == 0);

    const auto toggle = state_color(State::Switch, 10);
    assert(toggle.r == 10 && toggle.g == 0 && toggle.b == 10);

    const auto idle = state_color(State::Idle, 10);
    assert(idle.r == 0 && idle.g == 0 && idle.b == 0);

    const auto select_mode = display_color(State::Tv, true, 10);
    assert(select_mode.r == 255 && select_mode.g == 255 && select_mode.b == 255);

    const auto select_idle = display_color(State::Idle, true, 10);
    assert(select_idle.r == 0 && select_idle.g == 0 && select_idle.b == 0);
}

}  // namespace

int main() {
    test_state_names();
    test_number_transitions();
    test_light_and_unknown();
    test_colors();
    std::puts("state_preview tests passed");
    return 0;
}
