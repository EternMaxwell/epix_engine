#include "epix/module_config.hpp"

#if EPIX_HAS_MODULES
module;
export module epix.render;

#if EPIX_HAS_STD_MODULES
import std;
#else
#include <version>
#endif

export import "epix/render.h";
export import "epix/render/graph.h";
export import "epix/render/pipeline.h";
#else
#pragma once
#include "epix/render.h"
#include "epix/render/graph.h"
#include "epix/render/pipeline.h"
#endif
