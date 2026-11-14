#pragma once

#include "ir/ir_types.hpp"

namespace irhub::ir {

bool find_code(Device device, const char* command, const char*& code);

}  // namespace irhub::ir
