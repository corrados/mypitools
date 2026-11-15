#pragma once

#include <cstdint>

namespace irhub::input {

const char* ps3_map(uint64_t value);
const char* ps3_convert(const char* button);
bool ps3_is_repeat_key(const char* button);

class Ps3RepeatTracker {
public:
    void press(const char* button, uint32_t now_ms);
    void release();
    bool tick(uint32_t now_ms);
    bool active() const { return active_; }

private:
    bool active_ = false;
    uint32_t next_ms_ = 0;
    uint32_t interval_ms_ = 0;
};

}  // namespace irhub::input
