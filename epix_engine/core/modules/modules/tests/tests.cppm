module;

#include <gtest/gtest.h>

export module epix.core:tests;

import std;

namespace core::tests {
struct ForceBase {
    template <typename T>
    ForceBase(T&&) {}
    virtual ~ForceBase() {
        volatile auto unused = 0;
        unused++;  // prevent optimization
    }
};

export [[nodiscard]] std::vector<ForceBase> force_link_tests();
}  // namespace core::tests