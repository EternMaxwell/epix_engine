#include "epix/app/systemparam.h"

using namespace epix::app;

EPIX_API std::string SystemParamInfo::to_string() const {
    std::string str = "SystemParamInfo: \n";
    str += "has world: " + std::to_string(has_world) + "\n";
    str += "has commands: " + std::to_string(has_commands) + "\n";
    str += "query_types: \n";
    for (auto& query : query_types) {
        auto&& [get, with, without] = query;
        str += "  mutable: ";
        for (auto& type : get) {
            str += type.name() + std::string(", ");
        }
        str += "\n  const: ";
        for (auto& type : with) {
            str += type.name() + std::string(", ");
        }
        str += "\n  exclude: ";
        for (auto& type : without) {
            str += type.name() + std::string(", ");
        }
        str += "\n";
    }
    str += "read only resource: \n";
    for (auto& type : resource_types) {
        str += "  " + std::string(type.name()) + "\n";
    }
    str += "mutable resource: \n";
    for (auto& type : resource_muts) {
        str += "  " + std::string(type.name()) + "\n";
    }
    return str;
}

EPIX_API bool SystemParamInfo::conflict_with(const SystemParamInfo& other) const {
    if (has_world || other.has_world) {
        return true;
    }
    // use command and query at the same time is now always thread safe
    // if (has_command && (other.has_command || other.has_query)) {
    //     return true;
    // }
    // if (other.has_command && (has_command || has_query)) {
    //     return true;
    // }
    // if two systems use command at the same time, it is not thread safe
    if (has_commands && other.has_commands) {
        return true;
    }
    // check if queries conflict
    static auto query_conflict =
        [](const std::tuple<
               entt::dense_set<std::type_index>,
               entt::dense_set<std::type_index>,
               entt::dense_set<std::type_index>>& query,
           const std::tuple<
               entt::dense_set<std::type_index>,
               entt::dense_set<std::type_index>,
               entt::dense_set<std::type_index>>& other_query) -> bool {
        auto&& [get_a, with_a, without_a] = query;
        auto&& [get_b, with_b, without_b] = other_query;
        for (auto& type : without_a) {
            if (get_b.contains(type) || with_b.contains(type)) return false;
        }
        for (auto& type : without_b) {
            if (get_a.contains(type) || with_a.contains(type)) return false;
        }
        for (auto& type : get_a) {
            if (get_b.contains(type) || with_b.contains(type)) return true;
        }
        for (auto& type : get_b) {
            if (get_a.contains(type) || with_a.contains(type)) return true;
        }
        return false;
    };
    for (auto& query : query_types) {
        for (auto& other_query : other.query_types) {
            if (query_conflict(query, other_query)) {
                return true;
            }
        }
    }
    // check if resources conflict
    for (auto& res : resource_muts) {
        if (other.resource_types.contains(res) ||
            other.resource_muts.contains(res)) {
            return true;
        }
    }
    for (auto& res : other.resource_muts) {
        if (resource_types.contains(res) || resource_muts.contains(res)) {
            return true;
        }
    }
    return false;
}