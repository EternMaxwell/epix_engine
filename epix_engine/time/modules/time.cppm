module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <utility>
#endif

export module epix.time;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.core;
extern "C++" {
#include <epix/time.hpp>
}
