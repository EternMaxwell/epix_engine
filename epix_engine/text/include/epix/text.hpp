#pragma once

#include "text/font.hpp"
#include "text/render.hpp"
#include "text/text.hpp"

namespace epix::text {
struct TextPlugin {
    void build(App& app);
};
};  // namespace epix::text