#pragma once

#include <algorithm>
#include <ranges>

#include "../component.hpp"
#include "../storage/bitvector.hpp"
#include "../type_system/type_registry.hpp"

namespace epix::core::query {
struct Access {
   public:
    Access() = default;

    void add_component_read(TypeId type_id) {
        if (!component_read_writes_inverted) {
            component_read_writes.set(type_id.get());
        } else {
            component_read_writes.reset(type_id.get());
        }
    }
    void add_component_write(TypeId type_id) {
        if (!component_writes_inverted) {
            component_writes.set(type_id.get());
        } else {
            component_writes.reset(type_id.get());
        }
        // writes are also reads
        add_component_read(type_id);
    }
    void add_resource_read(TypeId type_id) { resource_read_writes.set(type_id.get()); }
    void add_resource_write(TypeId type_id) {
        resource_writes.set(type_id.get());
        // writes are also reads
        add_resource_read(type_id);
    }

    void remove_component_write(TypeId type_id) {
        if (!component_writes_inverted) {
            component_writes.reset(type_id.get());
        } else {
            component_writes.set(type_id.get());
        }
    }
    void remove_component_read(TypeId type_id) {
        if (!component_read_writes_inverted) {
            component_read_writes.reset(type_id.get());
        } else {
            component_read_writes.set(type_id.get());
        }
        remove_component_write(type_id);
    }
    void add_archetypal(TypeId type_id) { archetypal.set(type_id.get()); }

    bool has_component_read(TypeId type_id) const {
        return component_read_writes_inverted ^ component_read_writes.contains(type_id.get());
    }
    bool has_any_component_read() const { return component_read_writes_inverted || !component_read_writes.is_clear(); }
    bool has_component_write(TypeId type_id) const {
        return component_writes_inverted ^ component_writes.contains(type_id.get());
    }
    bool has_any_component_write() const { return component_writes_inverted || !component_writes.is_clear(); }
    bool has_resource_read(TypeId type_id) const {
        return reads_all_resources || resource_read_writes.contains(type_id.get());
    }
    bool has_any_resource_read() const { return reads_all_resources || !resource_read_writes.is_clear(); }
    bool has_resource_write(TypeId type_id) const {
        return writes_all_resources || resource_writes.contains(type_id.get());
    }
    bool has_any_resource_write() const { return writes_all_resources || !resource_writes.is_clear(); }

    bool has_any_read() const { return has_any_component_read() || has_any_resource_read(); }
    bool has_any_write() const { return has_any_component_write() || has_any_resource_write(); }

    bool has_archetypal(TypeId type_id) const { return archetypal.contains(type_id.get()); }

    void read_all_components() {
        component_read_writes_inverted = true;
        component_read_writes.clear();
    }
    void write_all_components() {
        component_writes_inverted = true;
        component_writes.clear();
        read_all_components();
    }
    void read_all_resources() { reads_all_resources = true; }
    void write_all_resources() {
        writes_all_resources = true;
        read_all_resources();
    }

    void read_all() {
        read_all_components();
        read_all_resources();
    }
    void write_all() {
        write_all_components();
        write_all_resources();
    }

    bool is_read_all_components() const { return component_read_writes_inverted && component_read_writes.is_clear(); }
    bool is_write_all_components() const { return component_writes_inverted && component_writes.is_clear(); }
    bool is_read_all_resources() const { return reads_all_resources; }
    bool is_write_all_resources() const { return writes_all_resources; }
    bool is_read_all() const { return is_read_all_components() && is_read_all_resources(); }
    bool is_write_all() const { return is_write_all_components() && is_write_all_resources(); }

    void clear_writes() {
        component_writes_inverted = false;
        component_writes.clear();
        writes_all_resources = false;
        resource_writes.clear();
    }
    void clear() {
        component_read_writes_inverted = false;
        component_read_writes.clear();
        component_writes_inverted = false;
        component_writes.clear();
        reads_all_resources = false;
        resource_read_writes.clear();
        writes_all_resources = false;
        resource_writes.clear();
        archetypal.clear();
    }

    void merge(const Access& other) {
        auto new_component_read_writes_inverted =
            component_read_writes_inverted || other.component_read_writes_inverted;
        auto new_component_writes_inverted = component_writes_inverted || other.component_writes_inverted;

        if (this->component_read_writes_inverted && other.component_read_writes_inverted) {
            this->component_read_writes.intersect_with(other.component_read_writes);
        } else if (this->component_read_writes_inverted && !other.component_read_writes_inverted) {
            this->component_read_writes.difference_with(other.component_read_writes);
        } else if (!this->component_read_writes_inverted && other.component_read_writes_inverted) {
            this->component_read_writes.resize(
                std::max(this->component_read_writes.size(), other.component_read_writes.size()));
            this->component_read_writes.toggle_all();
            this->component_read_writes.intersect_with(other.component_read_writes);
        } else {
            this->component_read_writes.union_with(other.component_read_writes);
        }

        if (this->component_writes_inverted && other.component_writes_inverted) {
            this->component_writes.intersect_with(other.component_writes);
        } else if (this->component_writes_inverted && !other.component_writes_inverted) {
            this->component_writes.difference_with(other.component_writes);
        } else if (!this->component_writes_inverted && other.component_writes_inverted) {
            this->component_writes.resize(std::max(this->component_writes.size(), other.component_writes.size()));
            this->component_writes.toggle_all();
            this->component_writes.intersect_with(other.component_writes);
        } else {
            this->component_writes.union_with(other.component_writes);
        }

        this->reads_all_resources            = this->reads_all_resources || other.reads_all_resources;
        this->writes_all_resources           = this->writes_all_resources || other.writes_all_resources;
        this->component_read_writes_inverted = new_component_read_writes_inverted;
        this->component_writes_inverted      = new_component_writes_inverted;
        this->resource_read_writes.union_with(other.resource_read_writes);
        this->resource_writes.union_with(other.resource_writes);
    }

    bool is_component_compatible(const Access& other) const {
        for (auto&& [lhs_writes, rhs_reads_writes, lhs_writes_inverted, rhs_reads_writes_inverted] :
             {std::tie(component_writes, other.component_read_writes, component_writes_inverted,
                       other.component_read_writes_inverted),
              std::tie(other.component_writes, component_read_writes, other.component_writes_inverted,
                       component_read_writes_inverted)}) {
            if (lhs_writes_inverted && rhs_reads_writes_inverted) {
                return false;
            } else if (lhs_writes_inverted && !rhs_reads_writes_inverted) {
                if (!rhs_reads_writes.is_subset(lhs_writes)) return false;
            } else if (!lhs_writes_inverted && rhs_reads_writes_inverted) {
                if (!lhs_writes.is_subset(rhs_reads_writes)) return false;
            } else {
                if (!lhs_writes.is_disjoint(rhs_reads_writes)) return false;
            }
        }
        return true;
    }
    bool is_resource_compatible(const Access& other) const {
        if (writes_all_resources) return !other.has_any_resource_read();
        if (other.writes_all_resources) return !has_any_resource_read();

        if (reads_all_resources) return other.has_any_resource_write();
        if (other.reads_all_resources) return has_any_resource_write();

        return resource_writes.is_disjoint(other.resource_read_writes) &&
               other.resource_writes.is_disjoint(resource_read_writes);
    }
    bool is_compatible(const Access& other) const {
        return is_component_compatible(other) && is_resource_compatible(other);
    }

   private:
    storage::bit_vector component_read_writes;
    storage::bit_vector component_writes;
    storage::bit_vector resource_read_writes;
    storage::bit_vector resource_writes;

    bool component_read_writes_inverted = false;
    bool component_writes_inverted      = false;

    bool reads_all_resources  = false;
    bool writes_all_resources = false;

    storage::bit_vector archetypal;
};
struct AccessFilters {
    storage::bit_vector with;
    storage::bit_vector without;

    bool ruled_out(const AccessFilters& other) const {
        return !with.is_disjoint(other.without) || !without.is_disjoint(other.with);
    }
};
struct FilteredAccess {
    static FilteredAccess matches_everything() {
        FilteredAccess fa;
        fa._filters.emplace_back();
        return fa;
    }
    static FilteredAccess matches_nothing() { return FilteredAccess(); }

    const Access& access() const { return _access; }
    Access& access_mut() { return _access; }
    const storage::bit_vector& required() const { return _required; }
    storage::bit_vector& required_mut() { return _required; }
    auto filters() const { return std::views::all(_filters); }
    auto filters_mut() { return std::views::all(_filters); }

    void add_required(TypeId type_id) { _required.set(type_id.get()); }
    void add_with(TypeId type_id) {
        for (auto& f : _filters) f.with.set(type_id.get());
    }
    void add_without(TypeId type_id) {
        for (auto& f : _filters) f.without.set(type_id.get());
    }
    void add_component_read(TypeId type_id) {
        _access.add_component_read(type_id);
        add_required(type_id);
        add_with(type_id);
    }
    void add_component_write(TypeId type_id) {
        _access.add_component_write(type_id);
        add_required(type_id);
        add_with(type_id);
    }
    void add_resource_read(TypeId type_id) { _access.add_resource_read(type_id); }
    void add_resource_write(TypeId type_id) { _access.add_resource_write(type_id); }

    void append_or(const FilteredAccess& other) { _filters.insert_range(_filters.end(), other._filters); }
    void merge_access(const FilteredAccess& other) { _access.merge(other._access); }

    bool is_compatible(const FilteredAccess& other) const {
        if (!_access.is_resource_compatible(other._access)) return false;
        if (_access.is_component_compatible(other._access)) return true;

        return std::ranges::all_of(_filters, [&](const AccessFilters& f1) {
            return std::ranges::all_of(other._filters, [&](const AccessFilters& f2) { return f1.ruled_out(f2); });
        });
    }
    void merge(const FilteredAccess& other) {
        _access.merge(other._access);
        _required.union_with(other._required);
        std::vector<AccessFilters> old_filters = std::move(_filters);
        for (auto&& [lhs, rhs] : std::views::cartesian_product(old_filters, other._filters)) {
            if (!lhs.ruled_out(rhs)) {
                AccessFilters af = lhs;
                af.with.union_with(rhs.with);
                af.without.union_with(rhs.without);
                _filters.emplace_back(std::move(af));
            }
        }
    }

    bool contains(TypeId type_id) const {
        return _access.has_component_read(type_id) || _access.has_archetypal(type_id) ||
               std::ranges::any_of(_filters, [&](const AccessFilters& f) {
                   return f.with.contains(type_id) || f.without.contains(type_id);
               });
    }

   private:
    Access _access;
    storage::bit_vector _required;
    std::vector<AccessFilters> _filters;
};

struct FilteredAccessSet {
   public:
    const Access& combined_access() const { return _combined_access; }
    bool is_compatible(const FilteredAccessSet& other) const {
        if (_combined_access.is_compatible(other._combined_access)) return true;
        for (auto&& [lhs, rhs] : std::views::cartesian_product(_filtered_access, other._filtered_access)) {
            if (!lhs.is_compatible(rhs)) return false;
        }
        return true;
    }
    void add(FilteredAccess fa) {
        _combined_access.merge(fa.access());
        _filtered_access.emplace_back(std::move(fa));
    }
    void extend(FilteredAccessSet other) {
        _combined_access.merge(other._combined_access);
        _filtered_access.insert_range(_filtered_access.end(), std::move(other._filtered_access));
    }

    void add_unfiltered_resource_read(TypeId type_id) {
        FilteredAccess fa;
        fa.add_resource_read(type_id);
        add(std::move(fa));
    }
    void add_unfiltered_resource_write(TypeId type_id) {
        FilteredAccess fa;
        fa.add_resource_write(type_id);
        add(std::move(fa));
    }
    void add_unfiltered_read_all_resources() {
        FilteredAccess fa;
        fa.access_mut().read_all_resources();
        add(std::move(fa));
    }
    void add_unfiltered_write_all_resources() {
        FilteredAccess fa;
        fa.access_mut().write_all_resources();
        add(std::move(fa));
    }

    void read_all() {
        FilteredAccess fa = FilteredAccess::matches_everything();
        fa.access_mut().read_all();
        add(std::move(fa));
    }
    void write_all() {
        FilteredAccess fa = FilteredAccess::matches_everything();
        fa.access_mut().write_all();
        add(std::move(fa));
    }

    void clear() {
        _combined_access.clear();
        _filtered_access.clear();
    }

   private:
    Access _combined_access;
    std::vector<FilteredAccess> _filtered_access;
};
};  // namespace epix::core::query