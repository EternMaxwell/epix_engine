#include "epix/module_config.hpp"

#if EPIX_HAS_MODULES
module;
export module epix.image;

#if EPIX_HAS_STD_MODULES
import std;
#else
#include <version>
#endif

export import "epix/image.h";
#else
#pragma once
#include "epix/image.h"
#endif
