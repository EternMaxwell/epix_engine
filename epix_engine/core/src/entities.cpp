module;

#include <cassert>

module epix.core;

import std;

import :entities;

namespace core {
void Entities::verify_flush() {
    assert(!needs_flush() && "Entities need to be flushed before accessing meta or pending!");
}

Entity Entities::alloc() {
    verify_flush();
    std::uint32_t idx = static_cast<std::uint32_t>(meta.size());
    if (!pending.empty()) {
        std::uint32_t index = pending.back();
        pending.pop_back();
        std::int64_t new_free_cursor = pending.size();
        free_cursor.store(new_free_cursor, std::memory_order_relaxed);
        return Entity::from_parts(index, meta[index].generation);
    } else {
        std::uint32_t index = static_cast<std::uint32_t>(meta.size());
        meta.push_back(EntityMeta::empty());
        return Entity::from_index(index);
    }
}

std::optional<EntityLocation> Entities::free(Entity entity) {
    verify_flush();

    auto& meta = this->meta[entity.index];
    if (meta.generation != entity.generation) {
        return std::nullopt;
    }

    meta.generation++;
    auto loc      = meta.location;
    meta.location = EntityLocation::invalid();

    pending.push_back(entity.index);
    std::int64_t new_free_cursor = pending.size();
    free_cursor.store(new_free_cursor, std::memory_order_relaxed);
    return loc;
}

void Entities::reserve(std::uint32_t count) {
    verify_flush();

    auto free_size    = free_cursor.load(std::memory_order_relaxed);
    auto reserve_size = static_cast<std::int64_t>(count) - free_size;
    if (reserve_size > 0) {
        meta.reserve(meta.size() + reserve_size);
    }
}

bool Entities::contains(Entity entity) const {
    return resolve_index(entity.index)
        .transform([this, &entity](Entity e) { return e.generation == entity.generation; })
        .value_or(false);
}

void Entities::clear() {
    meta.clear();
    pending.clear();
    free_cursor.store(0, std::memory_order_relaxed);
}

std::optional<EntityLocation> Entities::get(Entity entity) const {
    if (entity.index >= meta.size()) return std::nullopt;
    auto& meta = this->meta[entity.index];
    if (meta.generation != entity.generation ||
        meta.location.archetype_id.get() == std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return meta.location;
}

void Entities::set(std::uint32_t index, EntityLocation location) { meta[index].location = location; }

bool Entities::reserve_generations(std::uint32_t index, std::uint32_t generations) {
    if (index >= meta.size()) {
        return false;
    }
    if (meta[index].location.archetype_id.get() == std::numeric_limits<std::uint32_t>::max()) {
        meta[index].generation += generations;
        return true;
    }
    return false;
}

std::optional<Entity> Entities::resolve_index(std::uint32_t index) const {
    if (index < meta.size()) {
        return Entity::from_parts(index, meta[index].generation);
    } else {
        auto free = free_cursor.load(std::memory_order_relaxed);
        if (free > 0) return std::nullopt;
        if (index < meta.size() + static_cast<size_t>(-free)) {
            return Entity::from_index(index);
        } else {
            return std::nullopt;
        }
    }
}

bool Entities::needs_flush() const {
    return free_cursor.load(std::memory_order_relaxed) != static_cast<std::int64_t>(pending.size());
}

void Entities::flush_as_invalid() {
    flush([](Entity, EntityLocation& loc) { loc.archetype_id = std::numeric_limits<std::uint32_t>::max(); });
}

}  // namespace core
