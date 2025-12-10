#include "epix/module_config.hpp"

#if EPIX_HAS_MODULES
module;
export module epix.transform;

#if EPIX_HAS_STD_MODULES
import std;
#else
#include <version>
#endif

export import "epix/transform/transform.h";
export import "epix/transform/plugin.h";
#else
#pragma once
#include "epix/transform/transform.h"
#include "epix/transform/plugin.h"
#endif
