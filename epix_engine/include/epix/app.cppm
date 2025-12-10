#include "epix/module_config.hpp"

#if EPIX_HAS_MODULES
module;
export module epix.app;

#if EPIX_HAS_STD_MODULES
import std;
#else
#include <version>
#endif

export import "epix/app.h";
#else
#pragma once
#include "epix/app.h"
#endif
