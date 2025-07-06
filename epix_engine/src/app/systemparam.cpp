#include "epix/app/systemparam.h"

using namespace epix::app;

EPIX_API bool SystemMeta::conflict(const SystemMeta& a, const SystemMeta& b) {
    if (&a == &b) {
        return true;
    }
    if (a.world != b.world && a.extract_target != b.extract_target) {
        return false;
    }
    if (a.access.writes_all || b.access.writes_all) {
        return true;
    }
    if (a.access.commands && b.access.commands) {
        return true;
    }
    auto set_has_common = []<typename T>(
                              const entt::dense_set<T>& a,
                              const entt::dense_set<T>& b
                          ) -> bool {
        auto&& [check, compare] =
            a.size() < b.size() ? std::tie(a, b) : std::tie(b, a);
        for (auto& type : check) {
            if (compare.contains(type)) {
                return true;
            }
        }
        return false;
    };
    auto query_conflict = [&set_has_common](
                              const Access::queries_t& a,
                              const Access::queries_t& b
                          ) -> bool {
        if (set_has_common(a.component_excludes, b.component_reads) ||
            set_has_common(a.component_excludes, b.component_writes)) {
            return false;
        }
        if (set_has_common(b.component_excludes, a.component_reads) ||
            set_has_common(b.component_excludes, a.component_writes)) {
            return false;
        }
        if (set_has_common(a.component_writes, b.component_writes) ||
            set_has_common(a.component_writes, b.component_reads) ||
            set_has_common(b.component_writes, a.component_reads)) {
            return true;
        }
        return false;
    };
    for (auto&& [a, b] :
         std::views::cartesian_product(a.access.queries, b.access.queries)) {
        if (query_conflict(a, b)) {
            return true;
        }
    }
    if (set_has_common(a.access.resource_writes, b.access.resource_writes) ||
        set_has_common(a.access.resource_writes, b.access.resource_reads) ||
        set_has_common(b.access.resource_writes, a.access.resource_reads)) {
        return true;
    }
    return false;
}