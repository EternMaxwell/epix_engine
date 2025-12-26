module;

#include <algorithm>
#include <ranges>

module epix.core;

import :query.access;

namespace core {
void Access::merge(const Access& other) {
    auto new_component_read_writes_inverted = component_read_writes_inverted || other.component_read_writes_inverted;
    auto new_component_writes_inverted      = component_writes_inverted || other.component_writes_inverted;

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

bool Access::is_component_compatible(const Access& other) const {
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

bool Access::is_resource_compatible(const Access& other) const {
    if (writes_all_resources) return !other.has_any_resource_read();
    if (other.writes_all_resources) return !has_any_resource_read();

    if (reads_all_resources) return other.has_any_resource_write();
    if (other.reads_all_resources) return has_any_resource_write();

    return resource_writes.is_disjoint(other.resource_read_writes) &&
           other.resource_writes.is_disjoint(resource_read_writes);
}

bool Access::is_compatible(const Access& other) const {
    return is_component_compatible(other) && is_resource_compatible(other);
}

AccessConflicts Access::get_component_conflicts(const Access& other) const {
    AccessConflicts ac{.all = false, .ids = {}};
    for (auto&& [lhs_writes, rhs_reads_writes, lhs_writes_inverted, rhs_reads_writes_inverted] :
         {std::tie(component_writes, other.component_read_writes, component_writes_inverted,
                   other.component_read_writes_inverted),
          std::tie(other.component_writes, component_read_writes, other.component_writes_inverted,
                   component_read_writes_inverted)}) {
        if (lhs_writes_inverted && rhs_reads_writes_inverted) {
            ac.all = true;
            return ac;
        } else if (lhs_writes_inverted && !rhs_reads_writes_inverted) {
            auto temp = rhs_reads_writes;
            temp.difference_with(lhs_writes);
            ac.ids.union_with(temp);
        } else if (!lhs_writes_inverted && rhs_reads_writes_inverted) {
            auto temp = lhs_writes;
            temp.difference_with(rhs_reads_writes);
            ac.ids.union_with(temp);
        } else {
            auto temp = lhs_writes;
            temp.intersect_with(rhs_reads_writes);
            ac.ids.union_with(temp);
        }
    }
    return ac;
}

AccessConflicts Access::get_conflicts(const Access& other) const {
    auto ac = get_component_conflicts(other);
    if (writes_all_resources) {
        if (other.reads_all_resources) {
            ac.all = true;
            return ac;
        }
        ac.ids.union_with(other.resource_read_writes);
    }
    if (reads_all_resources) {
        if (other.writes_all_resources) {
            ac.all = true;
            return ac;
        }
        ac.ids.union_with(other.resource_writes);
    }
    if (other.writes_all_resources) {
        if (reads_all_resources) {
            ac.all = true;
            return ac;
        }
        ac.ids.union_with(resource_read_writes);
    }
    if (other.reads_all_resources) {
        if (writes_all_resources) {
            ac.all = true;
            return ac;
        }
        ac.ids.union_with(resource_writes);
    }
    ac.ids.set_range(resource_writes.intersection(other.resource_read_writes), true);
    ac.ids.set_range(other.resource_writes.intersection(resource_read_writes), true);
    return ac;
}

bool FilteredAccess::is_compatible(const FilteredAccess& other) const {
    if (!_access.is_resource_compatible(other._access)) return false;
    if (_access.is_component_compatible(other._access)) return true;
    return std::ranges::all_of(_filters, [&](const AccessFilters& f1) {
        return std::ranges::all_of(other._filters, [&](const AccessFilters& f2) { return f1.ruled_out(f2); });
    });
}

void FilteredAccess::merge(const FilteredAccess& other) {
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

AccessConflicts FilteredAccess::get_conflicts(const FilteredAccess& other) const {
    if (!is_compatible(other)) {
        return _access.get_conflicts(other._access);
    }
    return AccessConflicts{.all = false, .ids = {}};
}

bool FilteredAccessSet::is_compatible(const FilteredAccessSet& other) const {
    if (_combined_access.is_compatible(other._combined_access)) return true;
    for (auto&& [lhs, rhs] : std::views::cartesian_product(_filtered_access, other._filtered_access)) {
        if (!lhs.is_compatible(rhs)) return false;
    }
    return true;
}

AccessConflicts FilteredAccessSet::get_conflicts(const FilteredAccessSet& other) const {
    AccessConflicts conflicts;
    if (!_combined_access.is_compatible(other._combined_access)) {
        for (auto&& filtered : _filtered_access) {
            for (auto&& other_filtered : other._filtered_access) {
                conflicts.add(filtered.get_conflicts(other_filtered));
            }
        }
    }
    return conflicts;
}

AccessConflicts FilteredAccessSet::get_conflicts(const FilteredAccess& other) const {
    AccessConflicts conflicts;
    if (!_combined_access.is_compatible(other.access())) {
        for (auto&& filtered : _filtered_access) {
            conflicts.add(filtered.get_conflicts(other));
        }
    }
    return conflicts;
}
}  // namespace core