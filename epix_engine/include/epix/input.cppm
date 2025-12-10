#include "epix/module_config.hpp"

#if EPIX_HAS_MODULES
module;
export module epix.input;

#if EPIX_HAS_STD_MODULES
import std;
#else
#include <version>
#endif

export import "epix/input.h";
#else
#pragma once
#include "epix/input.h"
#endif
