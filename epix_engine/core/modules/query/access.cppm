module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <format>
#include <ranges>
#include <string>
#include <utility>
#include <vector>
#endif
export module epix.core:query.access;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :utils;
import :type_registry;

namespace epix::core {
/** @brief Tracks which component/resource accesses conflict between systems. */
export struct AccessConflicts {
    /** @brief Whether all accesses conflict. */
    bool all = false;
    /** @brief Bit set of conflicting type ids. */
    bit_vector ids;

    /** @brief Merge another AccessConflicts into this one. */
    void add(const AccessConflicts& other) {
        all = all || other.all;
        if (!all) ids.union_with(other.ids);
    }
    /** @brief Check whether there are no conflicts. */
    bool empty() const { return !all && ids.is_clear(); }
    /** @brief Get a human-readable string of the conflict set. */
    std::string to_string() const {
        if (all) return "[<all>]";
        return std::format("{}", ids.iter_ones());
    }
};
/** @brief Tracks read and write access to components and resources for conflict detection. */
export struct Access {
   public:
    Access() = default;

    /** @brief Record a component read access. */
    void add_component_read(TypeId type_id) {
        if (!component_read_writes_inverted) {
            component_read_writes.set(type_id.get());
        } else {
            component_read_writes.reset(type_id.get());
        }
    }
    /** @brief Record a component write access (implicitly also a read). */
    void add_component_write(TypeId type_id) {
        if (!component_writes_inverted) {
            component_writes.set(type_id.get());
        } else {
            component_writes.reset(type_id.get());
        }
        // writes are also reads
        add_component_read(type_id);
    }
    /** @brief Record a resource read access. */
    void add_resource_read(TypeId type_id) { resource_read_writes.set(type_id.get()); }
    /** @brief Record a resource write access (implicitly also a read). */
    void add_resource_write(TypeId type_id) {
        resource_writes.set(type_id.get());
        // writes are also reads
        add_resource_read(type_id);
    }

    /** @brief Remove a component write access. */
    void remove_component_write(TypeId type_id) {
        if (!component_writes_inverted) {
            component_writes.reset(type_id.get());
        } else {
            component_writes.set(type_id.get());
        }
    }
    /** @brief Remove a component read access (also removes write). */
    void remove_component_read(TypeId type_id) {
        if (!component_read_writes_inverted) {
            component_read_writes.reset(type_id.get());
        } else {
            component_read_writes.set(type_id.get());
        }
        remove_component_write(type_id);
    }
    /** @brief Mark a component as used only for archetype filtering (not data access). */
    void add_archetypal(TypeId type_id) { archetypal.set(type_id.get()); }

    /** @brief Check if a specific component has read access. */
    bool has_component_read(TypeId type_id) const {
        return component_read_writes_inverted ^ component_read_writes.contains(type_id.get());
    }
    /** @brief Check if any component has read access. */
    bool has_any_component_read() const { return component_read_writes_inverted || !component_read_writes.is_clear(); }
    /** @brief Check if a specific component has write access. */
    bool has_component_write(TypeId type_id) const {
        return component_writes_inverted ^ component_writes.contains(type_id.get());
    }
    /** @brief Check if any component has write access. */
    bool has_any_component_write() const { return component_writes_inverted || !component_writes.is_clear(); }
    /** @brief Check if a specific resource has read access. */
    bool has_resource_read(TypeId type_id) const {
        return reads_all_resources || resource_read_writes.contains(type_id.get());
    }
    /** @brief Check if any resource has read access. */
    bool has_any_resource_read() const { return reads_all_resources || !resource_read_writes.is_clear(); }
    /** @brief Check if a specific resource has write access. */
    bool has_resource_write(TypeId type_id) const {
        return writes_all_resources || resource_writes.contains(type_id.get());
    }
    /** @brief Check if any resource has write access. */
    bool has_any_resource_write() const { return writes_all_resources || !resource_writes.is_clear(); }

    /** @brief Check if any read access (component or resource) is recorded. */
    bool has_any_read() const { return has_any_component_read() || has_any_resource_read(); }
    /** @brief Check if any write access (component or resource) is recorded. */
    bool has_any_write() const { return has_any_component_write() || has_any_resource_write(); }

    /** @brief Check if a component is used for archetype filtering. */
    bool has_archetypal(TypeId type_id) const { return archetypal.contains(type_id.get()); }

    /** @brief Mark all components as read-accessed. */
    void read_all_components() {
        component_read_writes_inverted = true;
        component_read_writes.clear();
    }
    /** @brief Mark all components as write-accessed. */
    void write_all_components() {
        component_writes_inverted = true;
        component_writes.clear();
        read_all_components();
    }
    /** @brief Mark all resources as read-accessed. */
    void read_all_resources() { reads_all_resources = true; }
    /** @brief Mark all resources as write-accessed. */
    void write_all_resources() {
        writes_all_resources = true;
        read_all_resources();
    }

    /** @brief Mark all components and resources as read-accessed. */
    void read_all() {
        read_all_components();
        read_all_resources();
    }
    /** @brief Mark all components and resources as write-accessed. */
    void write_all() {
        write_all_components();
        write_all_resources();
    }

    /** @brief Check if all components are read-accessed. */
    bool is_read_all_components() const { return component_read_writes_inverted && component_read_writes.is_clear(); }
    /** @brief Check if all components are write-accessed. */
    bool is_write_all_components() const { return component_writes_inverted && component_writes.is_clear(); }
    /** @brief Check if all resources are read-accessed. */
    bool is_read_all_resources() const { return reads_all_resources; }
    /** @brief Check if all resources are write-accessed. */
    bool is_write_all_resources() const { return writes_all_resources; }
    /** @brief Check if all components and resources are read-accessed. */
    bool is_read_all() const { return is_read_all_components() && is_read_all_resources(); }
    /** @brief Check if all components and resources are write-accessed. */
    bool is_write_all() const { return is_write_all_components() && is_write_all_resources(); }

    /** @brief Clear all write access records. */
    void clear_writes() {
        component_writes_inverted = false;
        component_writes.clear();
        writes_all_resources = false;
        resource_writes.clear();
    }
    /** @brief Clear all access records. */
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

    /** @brief Merge another Access into this one. */
    void merge(const Access& other);
    /** @brief Check if component accesses are compatible (non-conflicting) with another Access. */
    bool is_component_compatible(const Access& other) const;
    /** @brief Check if resource accesses are compatible with another Access. */
    bool is_resource_compatible(const Access& other) const;
    /** @brief Check if all accesses are compatible with another Access. */
    bool is_compatible(const Access& other) const;
    /** @brief Get component-level conflicts with another Access. */
    AccessConflicts get_component_conflicts(const Access& other) const;
    /** @brief Get all conflicts (component and resource) with another Access. */
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
/** @brief Pair of with/without bit masks used for archetype-level filtering. */
export struct AccessFilters {
    /** @brief Bit mask of required ("with") component types. */
    bit_vector with;
    /** @brief Bit mask of excluded ("without") component types. */
    bit_vector without;

    /** @brief Check if this filter's with/without masks are mutually exclusive with another. */
    bool ruled_out(const AccessFilters& other) const {
        return !with.is_disjoint(other.without) || !without.is_disjoint(other.with);
    }
};
/** @brief Access descriptor with archetype filters, combining Access with with/without masks.
 *  Used to determine which archetypes a query matches and detect access conflicts. */
export struct FilteredAccess {
    /** @brief Create a FilteredAccess that matches all archetypes. */
    static FilteredAccess matches_everything() {
        FilteredAccess fa;
        fa._filters.emplace_back();
        return fa;
    }
    /** @brief Create a FilteredAccess that matches nothing. */
    static FilteredAccess matches_nothing() { return FilteredAccess(); }

    /** @brief Get the underlying Access. */
    const Access& access() const { return _access; }
    /** @brief Get a mutable reference to the underlying Access. */
    Access& access_mut() { return _access; }
    /** @brief Get the set of required component type ids. */
    const bit_vector& required() const { return _required; }
    /** @brief Get a mutable reference to the required component set. */
    bit_vector& required_mut() { return _required; }
    /** @brief Get a const view of the filter masks. */
    auto filters() const { return std::views::all(_filters); }
    /** @brief Get a mutable view of the filter masks. */
    auto filters_mut() { return std::views::all(_filters); }

    /** @brief Add a required component id. */
    void add_required(TypeId type_id) { _required.set(type_id.get()); }
    /** @brief Add a with-filter for the given component. */
    void add_with(TypeId type_id) {
        for (auto& f : _filters) f.with.set(type_id.get());
    }
    /** @brief Add a without-filter for the given component. */
    void add_without(TypeId type_id) {
        for (auto& f : _filters) f.without.set(type_id.get());
    }
    /** @brief Record a component read: adds access, required, and with-filter. */
    void add_component_read(TypeId type_id) {
        _access.add_component_read(type_id);
        add_required(type_id);
        add_with(type_id);
    }
    /** @brief Record a component write: adds access, required, and with-filter. */
    void add_component_write(TypeId type_id) {
        _access.add_component_write(type_id);
        add_required(type_id);
        add_with(type_id);
    }
    /** @brief Record a resource read. */
    void add_resource_read(TypeId type_id) { _access.add_resource_read(type_id); }
    /** @brief Record a resource write. */
    void add_resource_write(TypeId type_id) { _access.add_resource_write(type_id); }

    /** @brief Append another FilteredAccess's filters as alternative (OR) match paths. */
    void append_or(const FilteredAccess& other) { _filters.insert_range(_filters.end(), other._filters); }
    /** @brief Merge another FilteredAccess's underlying Access into this one. */
    void merge_access(const FilteredAccess& other) { _access.merge(other._access); }

    /** @brief Check if this filtered access is compatible with another. */
    bool is_compatible(const FilteredAccess& other) const;
    /** @brief Merge another FilteredAccess fully into this one. */
    void merge(const FilteredAccess& other);
    /** @brief Check if this filtered access references the given component. */
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

/** @brief Collection of FilteredAccess entries with a combined Access for conflict detection.
 *  Each system builds a FilteredAccessSet describing all its component/resource accesses. */
export struct FilteredAccessSet {
   public:
    /** @brief Get the combined Access of all entries. */
    const Access& combined_access() const { return _combined_access; }
    /** @brief Check if this set is compatible with another set. */
    bool is_compatible(const FilteredAccessSet& other) const;
    /** @brief Get conflicts between this set and another set. */
    AccessConflicts get_conflicts(const FilteredAccessSet& other) const;
    /** @brief Get conflicts between this set and a single FilteredAccess. */
    AccessConflicts get_conflicts(const FilteredAccess& other) const;
    /** @brief Add a FilteredAccess entry to this set. */
    void add(FilteredAccess fa) {
        _combined_access.merge(fa.access());
        _filtered_access.emplace_back(std::move(fa));
    }
    /** @brief Merge all entries from another set into this one. */
    void extend(FilteredAccessSet other) {
        _combined_access.merge(other._combined_access);
        _filtered_access.insert_range(_filtered_access.end(), std::move(other._filtered_access));
    }

    /** @brief Add an unfiltered resource read entry. */
    void add_unfiltered_resource_read(TypeId type_id) {
        FilteredAccess fa;
        fa.add_resource_read(type_id);
        add(std::move(fa));
    }
    /** @brief Add an unfiltered resource write entry. */
    void add_unfiltered_resource_write(TypeId type_id) {
        FilteredAccess fa;
        fa.add_resource_write(type_id);
        add(std::move(fa));
    }
    /** @brief Add an entry that reads all resources. */
    void add_unfiltered_read_all_resources() {
        FilteredAccess fa;
        fa.access_mut().read_all_resources();
        add(std::move(fa));
    }
    /** @brief Add an entry that writes all resources. */
    void add_unfiltered_write_all_resources() {
        FilteredAccess fa;
        fa.access_mut().write_all_resources();
        add(std::move(fa));
    }

    /** @brief Add a single entry that reads everything. */
    void read_all() {
        FilteredAccess fa = FilteredAccess::matches_everything();
        fa.access_mut().read_all();
        add(std::move(fa));
    }
    /** @brief Add a single entry that writes everything. */
    void write_all() {
        FilteredAccess fa = FilteredAccess::matches_everything();
        fa.access_mut().write_all();
        add(std::move(fa));
    }

    /** @brief Remove all entries and combined access. */
    void clear() {
        _combined_access.clear();
        _filtered_access.clear();
    }

   private:
    Access _combined_access;
    std::vector<FilteredAccess> _filtered_access;
};
}  // namespace core