module;

#ifdef EPIX_ENABLE_TEST
#include <gtest/gtest.h>
#endif

#include <cstddef>
#include <print>

export module epix.core:query.access.test;

import :query.access;

export inline struct instance_ptr_print {
    instance_ptr_print() {
        std::println("GTest instance ptr: {}", reinterpret_cast<std::uintptr_t>(::testing::UnitTest::GetInstance()));
    }
    void print() {
        std::println("GTest instance ptr: {}", reinterpret_cast<std::uintptr_t>(::testing::UnitTest::GetInstance()));
    }
} inst;

#ifdef EPIX_ENABLE_TEST
TEST(core_test, access) {
    using namespace core;
    // IDs (constructed directly from integers)
    TypeId A((size_t)1);
    TypeId B((size_t)2);
    TypeId C((size_t)3);
    TypeId D((size_t)64);
    TypeId E((size_t)130);

    // --- Basic add/remove and queries ---
    {
        Access acc;
        acc.add_component_read(A);
        EXPECT_TRUE(acc.has_component_read(A) && "A should be readable");
        EXPECT_TRUE(!acc.has_component_write(A) && "A should not be writable yet");

        acc.add_component_write(A);
        EXPECT_TRUE(acc.has_component_write(A) && "A should be writable after add_component_write");
        // writes are reads as well
        EXPECT_TRUE(acc.has_component_read(A) && "write implies read");

        acc.remove_component_write(A);
        EXPECT_TRUE(!acc.has_component_write(A) && "remove_component_write should clear write flag");

        acc.add_archetypal(B);
        EXPECT_TRUE(acc.has_archetypal(B) && "archetypal should be tracked");

        acc.remove_component_read(A);
        EXPECT_TRUE(!acc.has_component_read(A) && "remove_component_read should clear read");
    }

    // --- Range and inverted helpers ---
    {
        Access a;
        a.read_all_components();
        EXPECT_TRUE(a.is_read_all_components() && "read_all_components sets inverted read-all");
        a.write_all_components();
        EXPECT_TRUE(a.is_write_all_components() && "write_all_components sets inverted write-all");

        a.clear_writes();
        EXPECT_TRUE(!a.is_write_all_components() && "clear_writes should unset write_all_components");

        a.read_all_resources();
        EXPECT_TRUE(a.is_read_all_resources() && "read_all_resources");
        a.write_all_resources();
        EXPECT_TRUE(a.is_write_all_resources() && "write_all_resources");
    }

    // --- Merge behavior (non-inverted union semantics) ---
    {
        Access x;
        x.add_component_read(A);
        x.add_component_write(B);

        Access y;
        y.add_component_read(B);
        y.add_component_write(C);

        x.merge(y);
        // reads from both
        EXPECT_TRUE(x.has_component_read(A) && "merge keeps A read");
        EXPECT_TRUE(x.has_component_read(B) && "merge keeps B read");
        EXPECT_TRUE(x.has_component_read(C) && "merge adds C read via write->read");
        // writes union
        EXPECT_TRUE(x.has_component_write(B) && "merge keeps B write");
        EXPECT_TRUE(x.has_component_write(C) && "merge adds C write");
    }

    // --- Merge involving inverted flags (read_all + specific) ---
    {
        Access a;
        a.read_all_components();  // inverted reads

        Access b;
        b.add_component_read(A);
        b.add_component_write(D);

        // merging b into a should keep read-all semantics (inverted) but also set writes union
        a.merge(b);
        EXPECT_TRUE(a.is_read_all_components() && "inverted read-all preserved after merge");
        EXPECT_TRUE(a.has_component_write(D) && "writes from other are merged");
    }

    // --- Compatibility checks (components) ---
    {
        Access w;
        Access r;
        w.add_component_write(A);
        r.add_component_read(A);
        EXPECT_TRUE(!w.is_component_compatible(r) && "write/read conflict detected");

        r.clear();
        r.add_component_read(B);
        EXPECT_TRUE(w.is_component_compatible(r) && "different components should be compatible");

        Access w1;
        Access w2;
        w1.add_component_write(E);
        w2.add_component_write(E);
        EXPECT_TRUE(!w1.is_component_compatible(w2) && "write/write conflict on same high id");
    }

    // --- Compatibility checks (resources) ---
    {
        Access r1;
        Access r2;
        r1.add_resource_write(A);
        r2.add_resource_read(A);
        EXPECT_TRUE(!r1.is_resource_compatible(r2) && "resource write/read conflict");

        r2.clear();
        r2.add_resource_read(B);
        EXPECT_TRUE(r1.is_resource_compatible(r2) && "different resources should be compatible");
    }

    // --- AccessFilters and FilteredAccess behaviors ---
    {
        AccessFilters f1;
        f1.with.set(A.get());
        AccessFilters f2;
        f2.without.set(B.get());
        EXPECT_TRUE(!f1.ruled_out(f2) && "Not ruled out: does not exclude other's with");

        AccessFilters f3;
        f3.without.set(A.get());
        EXPECT_TRUE(f1.ruled_out(f3) && "Ruled out: f1 requires A, f3 forbids A");

        FilteredAccess fa1 = FilteredAccess::matches_everything();
        fa1.add_component_read(A);
        FilteredAccess fa2 = FilteredAccess::matches_everything();
        fa2.add_component_read(B);
        FilteredAccessSet fas;
        fas.add(std::move(fa1));
        fas.add(std::move(fa2));
        EXPECT_TRUE(fas.combined_access().has_any_read() && "combined_access from set should have reads");

        // append_or and merge_access
        FilteredAccess fa3;
        fa3.add_component_read(C);
        FilteredAccess fa4;
        fa4.add_component_write(C);
        fa3.append_or(fa4);
        // merge_access into a new one
        FilteredAccess m;
        m.merge_access(fa3);
        EXPECT_TRUE(m.access().has_any_read() || m.access().has_any_write());
    }
}
#endif