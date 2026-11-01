#pragma once
#include <cstdint>
#include <string>

#include "core/rate.h"

namespace br {

// Unix epoch microseconds from QueryPerformanceCounter: a shared, monotonic clock for every input.
int64_t nowUs();

// Local time of day, "HH:MM:SS:FF" at the given rate (non-drop-frame).
std::string formatTimecode(int64_t epochUs, bc::Rate rate);

}  // namespace br