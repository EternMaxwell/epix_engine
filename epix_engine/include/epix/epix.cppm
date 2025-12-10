#include "epix/module_config.hpp"

#if EPIX_HAS_MODULES
module;
export module epix;

#if EPIX_HAS_STD_MODULES
import std;
#else
#include <version>
#endif

export import epix.app;
export import epix.assets;
export import epix.image;
export import epix.input;
export import epix.render;
export import epix.transform;
export import epix.window;
export import "epix/utils/time.h";
export import "epix/world/sand.h";
#else
#pragma once
#include "epix/prelude.h"
#endif
