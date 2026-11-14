#pragma once

#include <cstdint>
#include <vector>

#include "ir/ir_types.hpp"

namespace irhub::ir {

using Waveform = std::vector<Pulse>;

bool build_waveform(Device device, const char* code, bool toggle, Waveform& out);
uint32_t carrier_frequency_hz(Device device);

}  // namespace irhub::ir
