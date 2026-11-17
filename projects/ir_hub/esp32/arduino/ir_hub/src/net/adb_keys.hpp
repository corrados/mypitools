#pragma once

#include <cstddef>

namespace irhub::adb {

bool build_shell_command(const char* key, char* out, size_t out_size);
bool is_numeric_key(const char* key);

}  // namespace irhub::adb
