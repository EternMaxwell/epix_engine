#pragma once

#include <concepts>
#include <epix/core.hpp>
#include <expected>
#include <optional>

#include "handle.hpp"
#include "index.hpp"

namespace epix::assets {
struct AssetServer;
template <typename T>
struct Entry {
    std::optional<T> asset = std::nullopt;
    uint32_t generation    = 0;
};

struct IndexOutOfBound {
    uint32_t index;
};
struct SlotEmpty {
    uint32_t index;
};
struct GenMismatch {
    uint32_t index;
    uint32_t current_gen;
    uint32_t expected_gen;
};
using AssetNotPresent = std::variant<AssetIndex, uuids::uuid>;

using AssetError = std::variant<IndexOutOfBound, SlotEmpty, GenMismatch, AssetNotPresent>;

template <typename T>
struct AssetStorage {
   private:
    // The storage is a vector of optional Entry<T>.
    // if the optional is not empty, it means that the slot is valid and the
    // index is not released or recycled.
    std::vector<std::optional<Entry<T>>> m_storage;
    uint32_t m_size;

   public:
    AssetStorage() : m_size(0) {}
    AssetStorage(const AssetStorage&)            = delete;
    AssetStorage(AssetStorage&&)                 = delete;
    AssetStorage& operator=(const AssetStorage&) = delete;
    AssetStorage& operator=(AssetStorage&&)      = delete;

    uint32_t size() const { return m_size; }
    bool empty() const { return m_size == 0; }

    void resize_slots(uint32_t index) { m_storage.resize(index + 1); }

    std::expected<Entry<T>*, AssetError> get_entry(const AssetIndex& index) {
        if (index.index() >= m_storage.size()) {
            return std::unexpected(IndexOutOfBound{index.index()});
        }
        if (!m_storage[index.index()]) {
            return std::unexpected(SlotEmpty{index.index()});
        }
        if (m_storage[index.index()]->generation != index.generation()) {
            return std::unexpected(
                GenMismatch{index.index(), m_storage[index.index()]->generation, index.generation()});
        }
        return &m_storage[index.index()].value();
    }
    std::expected<const Entry<T>*, AssetError> get_entry(const AssetIndex& index) const {
        if (index.index() >= m_storage.size()) {
            return std::unexpected(IndexOutOfBound{index.index()});
        }
        if (!m_storage[index.index()]) {
            return std::unexpected(SlotEmpty{index.index()});
        }
        if (m_storage[index.index()]->generation != index.generation()) {
            return std::unexpected(
                GenMismatch{index.index(), m_storage[index.index()]->generation, index.generation()});
        }
        return &m_storage[index.index()].value();
    }

    /**
     * @brief Insert an asset into the storage. If the asset already exists, it
     * will be replaced.
     *
     * @param index The index and generation of the asset to insert.
     * @param args The arguments to construct the asset.
     * @return std::optional<bool> True if the asset was replaced, false if it
     * was inserted. If the generation is different or if it is out of range,
     * std::nullopt is returned.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<bool, AssetError> insert(const AssetIndex& index, Args&&... args) {
        if (index.index() >= m_storage.size()) {
            return std::unexpected(IndexOutOfBound{index.index()});
        }
        if (!m_storage[index.index()]) {
            m_storage[index.index()]             = Entry<T>();
            m_storage[index.index()]->generation = index.generation();
        } else if (m_storage[index.index()]->generation != index.generation()) {
            return std::unexpected(
                GenMismatch{index.index(), m_storage[index.index()]->generation, index.generation()});
        }
        bool res = m_storage[index.index()]->asset.has_value();
        m_storage[index.index()]->asset.emplace(std::forward<Args>(args)...);
        m_size++;
        return res;
    }

    /**
     * @brief Check if the asset at the given index is valid and has a value.
     *
     * This means that the index is within bounds, the slot at this index is
     * available, the generation matches, and the asset has a value.
     *
     * @param index The index to check.
     * @return True if the asset is valid and has a value, false otherwise.
     */
    bool contains(const AssetIndex& index) const {
        return index.index() < m_storage.size() && m_storage[index.index()] &&
               m_storage[index.index()]->asset.has_value() &&
               m_storage[index.index()]->generation == index.generation();
    }

    /**
     * @brief Pop the asset at the given index. This will remove the asset from
     * the storage and return it. But the slot at this index is still valid.
     *
     * This is used in force remove and pop.
     *
     * @param index The index to pop.
     * @return The asset at the given index, or std::nullopt if the index
     */
    std::expected<T, AssetError> pop(const AssetIndex& index) {
        return get_entry(index).and_then([this, &index](Entry<T>* entry) -> std::expected<T, AssetError> {
            if (entry->asset.has_value()) {
                T asset = std::move(entry->asset.value());
                entry->asset.reset();
                m_size--;
                return std::move(asset);
            } else {
                return std::unexpected(AssetNotPresent(index));
            }
        });
    }

    /**
     * @brief Remove the asset at the given index. This will remove the asset
     * from the storage. But the slot at this index is still valid.
     *
     * This is used in force remove and pop.
     *
     * @param index The index to remove.
     * @return True if the asset was removed, false otherwise.
     */
    std::expected<void, AssetError> remove(const AssetIndex& index) {
        return get_entry(index).and_then([this, &index](Entry<T>* entry) -> std::expected<void, AssetError> {
            if (entry->asset.has_value()) {
                entry->asset.reset();
                m_size--;
                return {};
            } else {
                return std::unexpected(AssetNotPresent(index));
            }
        });
    }

    /**
     * @brief Remove the asset at the given index. This will remove the asset
     * from the storage and invalidate the slot at this index.
     *
     * This is used in handle destruction, where the index will be released and
     * recycled.
     *
     * @param index The index to remove.
     * @return True if the asset was removed, false otherwise.
     */
    std::expected<void, AssetError> remove_dereferenced(const AssetIndex& index) {
        return get_entry(index).and_then([this, &index](Entry<T>* entry) -> std::expected<void, AssetError> {
            if (entry->asset.has_value()) {
                entry->asset.reset();
                entry->generation++;
                m_size--;
                return {};
            } else {
                return std::unexpected(AssetNotPresent(index));
            }
        });
    }

    std::expected<T*, AssetError> try_get(const AssetIndex& index) {
        return get_entry(index).and_then([&index](Entry<T>* entry) -> std::expected<T*, AssetError> {
            if (entry->asset.has_value()) {
                return &entry->asset.value();
            } else {
                return std::unexpected(AssetNotPresent(index));
            }
        });
    }

    std::expected<const T*, AssetError> try_get(const AssetIndex& index) const {
        return get_entry(index).and_then([&index](const Entry<T>* entry) -> std::expected<const T*, AssetError> {
            if (entry->asset.has_value()) {
                return &entry->asset.value();
            } else {
                return std::unexpected(AssetNotPresent(index));
            }
        });
    }
    T* get(const AssetIndex& index) { return try_get(index).value_or(nullptr); }
    const T* get(const AssetIndex& index) const { return try_get(index).value_or(nullptr); }
};

template <typename T>
struct AssetEvent {
    enum class Type {
        Added,     // Asset added
        Removed,   // Asset removed
        Modified,  // Asset modified or replaced
        Unused,    // All strong handles destroyed
        Loaded,    // Asset loaded from disk or network
    } type;
    AssetId<T> id;

    static AssetEvent<T> added(const AssetId<T>& id) { return {Type::Added, id}; }
    static AssetEvent<T> removed(const AssetId<T>& id) { return {Type::Removed, id}; }
    static AssetEvent<T> modified(const AssetId<T>& id) { return {Type::Modified, id}; }
    static AssetEvent<T> unused(const AssetId<T>& id) { return {Type::Unused, id}; }
    static AssetEvent<T> loaded(const AssetId<T>& id) { return {Type::Loaded, id}; }
    bool is_added() const { return type == Type::Added; }
    bool is_removed() const { return type == Type::Removed; }
    bool is_modified() const { return type == Type::Modified; }
    bool is_unused() const { return type == Type::Unused; }
    bool is_loaded() const { return type == Type::Loaded; }
};

EPIX_API void log_asset_error(const AssetError& err, const std::string& header, const std::string_view& operation);

template <typename T>
    requires std::move_constructible<T> && std::is_move_assignable_v<T>
struct Assets {
   private:
    AssetStorage<T> m_assets;
    std::vector<uint32_t> m_references;
    std::shared_ptr<HandleProvider> m_handle_provider;
    std::unordered_map<uuids::uuid, T> m_mapped_assets;
    std::unordered_map<uuids::uuid, uint32_t> m_mapped_assets_ref;
    std::vector<AssetEvent<T>> m_cached_events;

    /**
     * @brief Insert an asset at the given index.
     *
     * @return `std::optional<bool>` True if the asset was replaced, false if
     * new value was inserted. `std::nullopt` if the index is invalid
     * (generation mismatch or no asset slot at given index).
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<bool, AssetError> insert_index(const AssetIndex& index, Args&&... args) {
        while (auto&& opt = m_handle_provider->index_allocator.reserved_receiver().try_receive()) {
            m_assets.resize_slots(opt->index());
            m_references.resize(opt->index() + 1, 0);
        }
        return m_assets.insert(index, std::forward<Args>(args)...)
            .or_else([this](AssetError&& err) -> std::expected<bool, AssetError> {
                log_asset_error(err, meta::type_id<Assets<T>>::name, "insert_index");
                return std::unexpected(std::move(err));
            });
    }
    /**
     * @brief Insert an asset at the given uuid.
     *
     * @param id The uuid of the asset to insert.
     * @param args The arguments to construct the asset.
     * @return `bool` True if the asset was replaced, false if new value was
     * inserted.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<bool, AssetError> insert_uuid(const uuids::uuid& id, Args&&... args) {
        if (contains(AssetId<T>(id))) {
            auto& asset = m_mapped_assets.at(id);
            asset.~T();
            new (&asset) T(std::forward<Args>(args)...);
            return true;
        }
        m_mapped_assets.emplace(id, std::forward<Args>(args)...);
        m_mapped_assets_ref.emplace(id, 0);
        return false;
    }

    bool release_index(const AssetIndex& index) {
        spdlog::debug("[{}] Releasing asset at {} with gen {}, current ref count is {}", meta::type_id<Assets<T>>::name,
                      index.index(), index.generation(), m_references[index.index()]);
        if (contains(AssetId<T>(index))) {
            m_references[index.index()]--;
            if (m_references[index.index()] == 0) {
                m_cached_events.emplace_back(AssetEvent<T>::unused(AssetId<T>(index)));
                m_assets.remove_dereferenced(index);
                m_handle_provider->index_allocator.release(index);
                m_cached_events.emplace_back(AssetEvent<T>::removed(AssetId<T>(index)));
                return true;
            }
        }
        return false;
    }
    bool release_uuid(const uuids::uuid& id) {
        if (contains(AssetId<T>(id))) {
            spdlog::debug("[{}] Releasing asset at Uuid:{}, current ref count is {}", meta::type_id<Assets<T>>::name,
                          uuids::to_string(id), m_mapped_assets_ref.at(id));
            auto& ref = m_mapped_assets_ref.at(id);
            ref--;
            if (ref == 0) {
                m_cached_events.emplace_back(AssetEvent<T>::unused(AssetId<T>(id)));
                m_mapped_assets.erase(id);
                m_mapped_assets_ref.erase(id);
                m_cached_events.emplace_back(AssetEvent<T>::removed(AssetId<T>(id)));
                return true;
            }
        }
        return false;
    }
    bool release(const AssetId<T>& id) {
        return std::visit(visitor{[this](const AssetIndex& index) { return release_index(index); },
                                  [this](const uuids::uuid& id) { return release_uuid(id); }},
                          id);
    }

   public:
    Assets() : m_handle_provider(std::make_shared<HandleProvider>(meta::type_id<T>{})) {}
    Assets(const Assets&)            = delete;
    Assets(Assets&&)                 = delete;
    Assets& operator=(const Assets&) = delete;
    Assets& operator=(Assets&&)      = delete;

    /**
     * @brief Get the handle provider for this assets collection.
     *
     * @return `std::shared_ptr<HandleProvider<T>>` The handle provider for this
     */
    std::shared_ptr<HandleProvider> get_handle_provider() const { return m_handle_provider; }

    /**
     * @brief Emplace an asset at a new index. This will create a new asset and
     * return a handle to it. The asset will be constructed in place with the
     * given arguments.
     *
     * @return `Handle<T>` The handle to the new asset.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    Handle<T> emplace(Args&&... args) {
        Handle<T> handle = m_handle_provider->reserve().typed<T>();
        spdlog::debug("[{}] Emplacing asset at {}", meta::type_id<Assets<T>>::name, handle.id().to_string_short());
        auto res = insert(handle, std::forward<Args>(args)...);
        std::visit(
            visitor{[this](const AssetIndex& index) { m_references[index.index()]++; }, [this](const auto& id) {}},
            handle.id());
        return handle;
    }

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<bool, AssetError> insert(const AssetId<T>& id, Args&&... args) {
        return std::visit(visitor{[this, &args...](const AssetIndex& index) mutable {
                                      return insert_index(index, std::forward<Args>(args)...);
                                  },
                                  [this, &args...](const uuids::uuid& id) mutable {
                                      return insert_uuid(id, std::forward<Args>(args)...);
                                  }},
                          id)
            .and_then([this, &id](bool replace) -> std::expected<bool, AssetError> {
                if (replace) {
                    spdlog::debug("[{}] Replaced asset at {}", meta::type_id<Assets<T>>::name, id.to_string_short());
                    m_cached_events.emplace_back(AssetEvent<T>::modified(id));
                } else {
                    spdlog::debug("[{}] Added asset at {}", meta::type_id<Assets<T>>::name, id.to_string_short());
                    m_cached_events.emplace_back(AssetEvent<T>::added(id));
                    m_cached_events.emplace_back(AssetEvent<T>::modified(id));
                }
                return replace;
            });
    }

    /**
     * @brief Check if there is an asset at the given index.
     *
     * This is used internally.
     */
    bool contains(const AssetId<T>& id) const {
        return std::visit(visitor{[this](const AssetIndex& index) { return m_assets.contains(index); },
                                  [this](const uuids::uuid& id) {
                                      return m_mapped_assets.contains(id) && m_mapped_assets_ref.contains(id);
                                  }},
                          id);
    }

    /**
     * @brief Create a strong handle to the asset at the given index.
     *
     * This will increment the reference count of the asset and return a strong
     * handle to it. If the asset is not valid, std::nullopt is returned.
     *
     * @param index The index of the asset to get a handle to.
     * @return `std::optional<Handle<T>>` The strong handle to the asset, or
     * std::nullopt if the asset is not valid.
     */
    std::expected<Handle<T>, AssetError> get_strong_handle(const AssetId<T>& id) {
        return try_get(id).and_then([this, &id](const T* asset) -> std::expected<Handle<T>, AssetError> {
            std::visit(visitor{[this](const AssetIndex& index) { m_references[index.index()]++; },
                               [this](const uuids::uuid& id) { m_mapped_assets_ref.at(id)++; }},
                       id);
            return m_handle_provider->get_handle(id, false, std::nullopt);
        });
    }

    /**
     * @brief Get the asset at the given index.
     *
     * This will return a reference to the asset at the given index. If the
     * asset is not valid, std::nullopt is returned.
     *
     * @param index The index of the asset to get.
     * @return `std::optional<std::reference_wrapper<T>>` A reference to the
     * asset, or std::nullopt if the asset is not valid.
     */
    const T* get(const AssetId<T>& id) const { return try_get(id).value_or(nullptr); }
    std::expected<const T*, AssetError> try_get(const AssetId<T>& id) const {
        return std::visit(visitor{[this](const AssetIndex& index) { return m_assets.try_get(index); },
                                  [this](const uuids::uuid& id) -> std::expected<const T*, AssetError> {
                                      if (auto&& it = m_mapped_assets.find(id); it != m_mapped_assets.end()) {
                                          return &it->second;
                                      } else {
                                          return std::unexpected(AssetNotPresent(id));
                                      }
                                  }},
                          id);
    }

    /**
     * @brief Get the asset at the given index.
     *
     * This will return a reference to the asset at the given index. If the
     * asset is not valid, std::nullopt is returned.
     *
     * @param index The index of the asset to get.
     * @return `std::optional<std::reference_wrapper<T>>` A reference to the
     * asset, or std::nullopt if the asset is not valid.
     */
    T* get_mut(const AssetId<T>& id) { return try_get_mut(id).value_or(nullptr); }
    std::expected<T*, AssetError> try_get_mut(const AssetId<T>& id) {
        return std::visit(visitor{[this](const AssetIndex& index) { return m_assets.try_get(index); },
                                  [this](const uuids::uuid& id) -> std::expected<T*, AssetError> {
                                      if (auto&& it = m_mapped_assets.find(id); it != m_mapped_assets.end()) {
                                          return &it->second;
                                      } else {
                                          return std::unexpected(AssetNotPresent(id));
                                      }
                                  }},
                          id)
            .and_then([this, &id](T* asset) -> std::expected<T*, AssetError> {
                if (asset) {
                    m_cached_events.emplace_back(AssetEvent<T>::modified(id));
                    return asset;
                } else {
                    return std::unexpected(AssetNotPresent(id));
                }
            });
    }

    /**
     * @brief Remove the asset at the given index. This will remove the asset
     * from the storage but not invalidate the slot at this index.
     *
     * @param index The index of the asset to remove.
     * @return `bool` True if the operation was successful, false otherwise.
     */
    std::expected<void, AssetError> remove(const AssetId<T>& id) {
        return std::visit(visitor{[this, &id](const AssetIndex& index) { return m_assets.remove(index); },
                                  [this, &id](const uuids::uuid& uuid) -> std::expected<void, AssetError> {
                                      if (contains(id)) {
                                          m_mapped_assets.erase(uuid);
                                          m_mapped_assets_ref.erase(uuid);
                                          return {};
                                      } else {
                                          return std::unexpected(AssetNotPresent(uuid));
                                      }
                                  }},
                          id)
            .and_then([this, &id](void) -> std::expected<void, AssetError> {
                spdlog::debug("[{}] Removed asset at {}", meta::type_id<Assets<T>>::name, id.to_string_short());
                m_cached_events.emplace_back(AssetEvent<T>::removed(id));
                return {};
            });
    }

    /**
     * @brief Pop the asset at the given index. This will remove the asset from
     * the storage but not invalidate the slot at this index.
     *
     * @param index The index of the asset to pop.
     * @return `std::optional<T>` The asset at the given index, or std::nullopt
     * if the asset is not valid.
     */
    std::expected<T, AssetError> take(const AssetId<T>& id) {
        return std::visit(visitor{[this](const AssetIndex& index) { return m_assets.pop(index); },
                                  [this](const uuids::uuid& id) -> std::expected<T, AssetError> {
                                      if (contains(id)) {
                                          auto asset = std::move(m_mapped_assets.at(id));
                                          m_mapped_assets.erase(id);
                                          m_mapped_assets_ref.erase(id);
                                          return std::move(asset);
                                      } else {
                                          return std::unexpected(AssetNotPresent(id));
                                      }
                                  }},
                          id)
            .and_then([this, &id](T&& asset) -> std::expected<T, AssetError> {
                spdlog::debug("[{}] Popped asset at {}", meta::type_id<Assets<T>>::name, id.to_string_short());
                m_cached_events.emplace_back(AssetEvent<T>::removed(id));
                return std::move(asset);
            });
    }

    void handle_events_internal(const AssetServer* asset_server = nullptr);
    /**
     * @brief Handle strong handle destruction events.
     */
    static void handle_events(epix::ResMut<Assets<T>> assets, epix::Res<AssetServer> asset_server) {
        assets->handle_events_internal(asset_server);
    }

    static void asset_events(epix::ResMut<Assets<T>> assets, epix::EventWriter<AssetEvent<T>> writer) {
        for (auto&& event : assets->m_cached_events) {
            writer.write(event);
        }
        assets->m_cached_events.clear();
    }
};
}  // namespace epix::assets