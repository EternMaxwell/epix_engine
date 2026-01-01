module;

export module epix.core:query.access;

import std;

import :utils;
import :type_registry;

namespace core {
export struct AccessConflicts {
    bool all = false;
    bit_vector ids;

    void add(const AccessConflicts& other) {
        all = all || other.all;
        if (!all) ids.union_with(other.ids);
    }
    bool empty() const { return !all && ids.is_clear(); }
    std::string to_string() const {
        if (all) return "[<all>]";
        return std::format("{}", ids.iter_ones());
    }
};
export struct Access {
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

    void merge(const Access& other);
    bool is_component_compatible(const Access& other) const;
    bool is_resource_compatible(const Access& other) const;
    bool is_compatible(const Access& other) const;
    AccessConflicts get_component_conflicts(const Access& other) const;
    AccessConflicts get_conflicts(const Access& other) const;

   private:
    bit_vector component_read_writes;
    bit_vector component_writes;
    bit_vector resource_read_writes;
    bit_vector resource_writes;

    bool component_read_writes_inverted = false;
    bool component_writes_inverted      = false;

    bool reads_all_resources  = false;
    bool writes_all_resources = false;

    bit_vector archetypal;
};
export struct AccessFilters {
    bit_vector with;
    bit_vector without;

    bool ruled_out(const AccessFilters& other) const {
        return !with.is_disjoint(other.without) || !without.is_disjoint(other.with);
    }
};
export struct FilteredAccess {
    static FilteredAccess matches_everything() {
        FilteredAccess fa;
        fa._filters.emplace_back();
        return fa;
    }
    static FilteredAccess matches_nothing() { return FilteredAccess(); }

    const Access& access() const { return _access; }
    Access& access_mut() { return _access; }
    const bit_vector& required() const { return _required; }
    bit_vector& required_mut() { return _required; }
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

    bool is_compatible(const FilteredAccess& other) const;
    void merge(const FilteredAccess& other);
    bool contains(TypeId type_id) const {
        return _access.has_component_read(type_id) || _access.has_archetypal(type_id) ||
               std::ranges::any_of(_filters, [&](const AccessFilters& f) {
                   return f.with.contains(type_id) || f.without.contains(type_id);
               });
    }

    AccessConflicts get_conflicts(const FilteredAccess& other) const;

   private:
    Access _access;
    bit_vector _required;
    std::vector<AccessFilters> _filters;
};

export struct FilteredAccessSet {
   public:
    const Access& combined_access() const { return _combined_access; }
    bool is_compatible(const FilteredAccessSet& other) const;
    AccessConflicts get_conflicts(const FilteredAccessSet& other) const;
    AccessConflicts get_conflicts(const FilteredAccess& other) const;
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
}  // namespace core