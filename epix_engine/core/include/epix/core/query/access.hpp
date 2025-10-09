#pragma once

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
};  // namespace epix::core::query