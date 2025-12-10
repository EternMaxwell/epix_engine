#include "epix/module_config.hpp"

#if EPIX_HAS_MODULES
module;
export module epix.window;

#if EPIX_HAS_STD_MODULES
import std;
#else
#include <version>
#endif

export import "epix/window.h";
#else
#pragma once
#include "epix/window.h"
#endif
