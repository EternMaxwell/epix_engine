module;

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

export module epix.assets:store;

import std;

import :handle;
import epix.utils;
using epix::utils::visitor;

namespace epix::assets {
/** @brief Forward declaration of AssetServer. */
export struct AssetServer;

bool asset_server_process_handle_destruction(const AssetServer& server, const UntypedAssetId& id);

template <typename T>
struct Entry {
    std::optional<T> asset   = std::nullopt;
    std::uint32_t generation = 0;
};

/** @brief Error: the requested slot index exceeds storage bounds. */
export struct IndexOutOfBound {
    /** @brief The index that was out of bounds. */
    std::uint32_t index;
};
/** @brief Error: the slot at the given index has been released. */
export struct SlotEmpty {
    /** @brief The slot index that was empty. */
    std::uint32_t index;
};
/** @brief Error: the generation counter of the slot does not match the request. */
export struct GenMismatch {
    /** @brief The slot index accessed. */
    std::uint32_t index;
    /** @brief The generation currently stored in the slot. */
    std::uint32_t current_gen;
    /** @brief The generation the caller expected. */
    std::uint32_t expected_gen;
};
/** @brief Error: an asset with the given identifier is not present. */
export using AssetNotPresent = std::variant<AssetIndex, uuids::uuid>;

/** @brief Sum type of all possible asset access errors. */
export using AssetError = std::variant<IndexOutOfBound, SlotEmpty, GenMismatch, AssetNotPresent>;

template <typename T>
struct AssetStorage {
   private:
    // The storage is a vector of optional Entry<T>.
    // if the optional is not empty, it means that the slot is valid and the
    // index is not released or recycled.
    std::vector<std::optional<Entry<T>>> m_storage;
    std::uint32_t m_size;

   public:
    AssetStorage() : m_size(0) {}
    AssetStorage(const AssetStorage&)            = delete;
    AssetStorage(AssetStorage&&)                 = delete;
    AssetStorage& operator=(const AssetStorage&) = delete;
    AssetStorage& operator=(AssetStorage&&)      = delete;

    std::uint32_t size() const { return m_size; }
    bool empty() const { return m_size == 0; }

    void resize_slots(std::uint32_t new_size) { m_storage.resize(new_size); }

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
            entry->generation++;
            if (entry->asset.has_value()) {
                entry->asset.reset();
                m_size--;
                return {};
            } else {
                return std::unexpected(AssetNotPresent(index));
            }
        });
    }

    std::expected<std::reference_wrapper<T>, AssetError> try_get_mut(const AssetIndex& index) {
        return get_entry(index).and_then(
            [&index](Entry<T>* entry) -> std::expected<std::reference_wrapper<T>, AssetError> {
                if (entry->asset.has_value()) {
                    return std::ref(entry->asset.value());
                } else {
                    return std::unexpected(AssetNotPresent(index));
                }
            });
    }

    std::expected<std::reference_wrapper<const T>, AssetError> try_get(const AssetIndex& index) const {
        return get_entry(index).and_then(
            [&index](const Entry<T>* entry) -> std::expected<std::reference_wrapper<const T>, AssetError> {
                if (entry->asset.has_value()) {
                    return std::cref(entry->asset.value());
                } else {
                    return std::unexpected(AssetNotPresent(index));
                }
            });
    }
    std::optional<std::reference_wrapper<T>> get_mut(const AssetIndex& index) {
        auto res = try_get_mut(index);
        return res.has_value() ? std::make_optional<std::reference_wrapper<T>>(res.value()) : std::nullopt;
    }
    std::optional<std::reference_wrapper<const T>> get(const AssetIndex& index) const {
        auto res = try_get(index);
        return res.has_value() ? std::make_optional<std::reference_wrapper<const T>>(res.value()) : std::nullopt;
    }

    /** @brief Call fn(uint32_t slot_index, uint32_t generation, const T& asset) for each valid entry. */
    template <typename F>
    void for_each(F&& fn) const {
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(m_storage.size()); i++) {
            auto& slot = m_storage[i];
            if (slot && slot->asset.has_value()) {
                fn(i, slot->generation, slot->asset.value());
            }
        }
    }
    /** @brief Call fn(uint32_t slot_index, uint32_t generation, T& asset) for each valid entry (mutable). */
    template <typename F>
    void for_each_mut(F&& fn) {
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(m_storage.size()); i++) {
            auto& slot = m_storage[i];
            if (slot && slot->asset.has_value()) {
                fn(i, slot->generation, slot->asset.value());
            }
        }
    }
};

/** @brief Lifecycle event for an asset of type T.
 *  Created automatically by Assets<T> when assets are added, removed,
 *  modified, when all strong handles are dropped, or when recursive
 *  dependencies have finished loading. */
export template <typename T>
struct AssetEvent {
    /** @brief Event kind discriminator. */
    enum class Type {
        Added,                 /**< Asset was newly inserted. */
        Removed,               /**< Asset was removed from storage. */
        Modified,              /**< Asset value was replaced or mutably accessed. */
        Unused,                /**< All strong handles have been destroyed. */
        LoadedWithDependencies /**< Asset and all its dependencies finished loading. */
    } type;
    /** @brief The id of the asset this event refers to. */
    AssetId<T> id;

    /** @brief Create an Added event. */
    static AssetEvent<T> added(const AssetId<T>& id) { return {Type::Added, id}; }
    /** @brief Create a Removed event. */
    static AssetEvent<T> removed(const AssetId<T>& id) { return {Type::Removed, id}; }
    /** @brief Create a Modified event. */
    static AssetEvent<T> modified(const AssetId<T>& id) { return {Type::Modified, id}; }
    /** @brief Create an Unused event (all strong handles dropped). */
    static AssetEvent<T> unused(const AssetId<T>& id) { return {Type::Unused, id}; }
    /** @brief Create a LoadedWithDependencies event. */
    static AssetEvent<T> loaded_with_dependencies(const AssetId<T>& id) { return {Type::LoadedWithDependencies, id}; }

    /** @brief Check if this is an Added event. */
    bool is_added() const { return type == Type::Added; }
    /** @brief Check if this is a Removed event. */
    bool is_removed() const { return type == Type::Removed; }
    /** @brief Check if this is a Modified event. */
    bool is_modified() const { return type == Type::Modified; }
    /** @brief Check if this is an Unused event. */
    bool is_unused() const { return type == Type::Unused; }
    /** @brief Check if this is a LoadedWithDependencies event. */
    bool is_loaded_with_dependencies() const { return type == Type::LoadedWithDependencies; }

    /** @brief Check if this is an Added event for a specific asset. */
    bool is_added(const AssetId<T>& asset_id) const { return type == Type::Added && id == asset_id; }
    /** @brief Check if this is a Removed event for a specific asset. */
    bool is_removed(const AssetId<T>& asset_id) const { return type == Type::Removed && id == asset_id; }
    /** @brief Check if this is a Modified event for a specific asset. */
    bool is_modified(const AssetId<T>& asset_id) const { return type == Type::Modified && id == asset_id; }
    /** @brief Check if this is an Unused event for a specific asset. */
    bool is_unused(const AssetId<T>& asset_id) const { return type == Type::Unused && id == asset_id; }
    /** @brief Check if this is a LoadedWithDependencies event for a specific asset. */
    bool is_loaded_with_dependencies(const AssetId<T>& asset_id) const {
        return type == Type::LoadedWithDependencies && id == asset_id;
    }
};

void log_asset_error(const AssetError& err, const std::string_view& header, const std::string_view& operation);

/** @brief A collection of asset handles loaded from a directory/folder.
 *  Matches bevy_asset's LoadedFolder. */
export struct LoadedFolder {
    /** @brief Handles to all assets loaded from the folder. */
    std::vector<UntypedHandle> handles;
};

/** @brief Wrapper for an untyped asset that has been fully loaded.
 *  Matches bevy_asset's LoadedUntypedAsset. */
export struct LoadedUntypedAsset {
    /** @brief Handle to the loaded asset. */
    UntypedHandle handle;
};

/** @brief Event fired when an asset load fails (typed).
 *  @tparam T The expected asset type. */
export template <typename T>
struct AssetLoadFailedEvent {
    /** @brief The id of the asset that failed to load. */
    AssetId<T> id;
    /** @brief The path that was attempted. */
    AssetPath path;
    /** @brief The load error that occurred. */
    std::variant<std::string, std::exception_ptr> error;
};

/** @brief Event fired when an asset load fails (untyped). */
export struct UntypedAssetLoadFailedEvent {
    /** @brief The id of the asset that failed to load. */
    UntypedAssetId id;
    /** @brief The path that was attempted. */
    AssetPath path;
    /** @brief The load error that occurred. */
    std::variant<std::string, std::exception_ptr> error;
};

/** @brief Collection that stores and manages assets of type T.
 *  @tparam T The asset type (must be movable). */
export template <std::movable T>
struct Assets {
   private:
    AssetStorage<T> m_assets;
    std::vector<std::uint32_t> m_references;
    std::shared_ptr<HandleProvider> m_handle_provider;
    std::unordered_map<uuids::uuid, T> m_mapped_assets;
    std::unordered_map<uuids::uuid, std::uint32_t> m_mapped_assets_ref;
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
        std::uint32_t storage_size = static_cast<std::uint32_t>(m_references.size());
        while (auto&& opt = m_handle_provider->index_allocator.reserved_receiver().try_receive()) {
            storage_size = std::max(storage_size, opt->index() + 1);
        }
        if (storage_size > m_references.size()) {
            m_assets.resize_slots(storage_size);
            m_references.resize(storage_size, 0);
        }
        return m_assets.insert(index, std::forward<Args>(args)...)
            .or_else([this](AssetError&& err) -> std::expected<bool, AssetError> {
                log_asset_error(err, meta::type_id<Assets<T>>::short_name(), "insert_index");
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
        if (index.index() < m_references.size()) {
            m_references[index.index()]--;
            if (m_references[index.index()] == 0) {
                m_cached_events.emplace_back(AssetEvent<T>::unused(AssetId<T>(index)));
                m_assets.remove_dereferenced(index).transform(
                    [&]() { m_cached_events.emplace_back(AssetEvent<T>::removed(AssetId<T>(index))); });
                m_handle_provider->index_allocator.release(index);
                return true;
            }
        }
        return false;
    }
    bool release_uuid(const uuids::uuid& id) {
        if (m_mapped_assets_ref.contains(id)) {
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
    /** @brief Construct a new Assets collection with its own HandleProvider. */
    Assets() : m_handle_provider(std::make_shared<HandleProvider>(meta::type_id<T>{})) {}
    Assets(const Assets&)            = delete;
    Assets(Assets&&)                 = delete;
    Assets& operator=(const Assets&) = delete;
    Assets& operator=(Assets&&)      = delete;

    /**
     * @brief Get the handle provider for this assets collection.
     *
     * @return The handle provider shared pointer.
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
        Handle<T> handle                                        = m_handle_provider->reserve().typed<T>();
        auto res                                                = insert(handle, std::forward<Args>(args)...);
        m_references[std::get<AssetIndex>(handle.id()).index()] = 1;
        return handle;
    }

    /**
     * @brief Insert an asset at the given id.
     *
     * @param id The id of the asset to insert.
     * @param args The arguments to construct the asset.
     * @return `std::expected<bool, AssetError>` value indicating whether the asset is replaced or error when error
     * occurs.
     */
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
                    m_cached_events.emplace_back(AssetEvent<T>::modified(id));
                } else {
                    m_cached_events.emplace_back(AssetEvent<T>::added(id));
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
     * @param id The asset identifier.
     * @return The strong handle to the asset, or an error if not found.
     */
    std::expected<Handle<T>, AssetError> get_strong_handle(const AssetId<T>& id) {
        return try_get(id).and_then([this, &id](const T& asset) -> std::expected<Handle<T>, AssetError> {
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
     * @param id The asset identifier.
     * @return A const reference to the asset, or std::nullopt if not found.
     */
    std::optional<std::reference_wrapper<const T>> get(const AssetId<T>& id) const {
        auto res = try_get(id);
        return res.has_value() ? std::make_optional<std::reference_wrapper<const T>>(res.value()) : std::nullopt;
    }
    /** @brief Try to get a const reference to the asset, returning an error on failure. */
    std::expected<std::reference_wrapper<const T>, AssetError> try_get(const AssetId<T>& id) const {
        return std::visit(
            visitor{[this](const AssetIndex& index) { return m_assets.try_get(index); },
                    [this](const uuids::uuid& id) -> std::expected<std::reference_wrapper<const T>, AssetError> {
                        if (auto&& it = m_mapped_assets.find(id); it != m_mapped_assets.end()) {
                            return std::cref(it->second);
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
     * @param id The asset identifier.
     * @return A mutable reference to the asset, or std::nullopt if not found.
     */
    std::optional<std::reference_wrapper<T>> get_mut(const AssetId<T>& id) {
        auto res = try_get_mut(id);
        return res.has_value() ? std::make_optional<std::reference_wrapper<T>>(res.value()) : std::nullopt;
    }
    /** @brief Try to get a mutable reference to the asset, returning an error on failure. */
    std::expected<std::reference_wrapper<T>, AssetError> try_get_mut(const AssetId<T>& id) {
        return std::visit(
                   visitor{[this](const AssetIndex& index) { return m_assets.try_get_mut(index); },
                           [this](const uuids::uuid& id) -> std::expected<std::reference_wrapper<T>, AssetError> {
                               if (auto&& it = m_mapped_assets.find(id); it != m_mapped_assets.end()) {
                                   return std::ref(it->second);
                               } else {
                                   return std::unexpected(AssetNotPresent(id));
                               }
                           }},
                   id)
            .transform([this, &id](std::reference_wrapper<T> asset) {
                m_cached_events.emplace_back(AssetEvent<T>::modified(id));
                return asset;
            });
    }

    /**
     * @brief Remove the asset at the given index. This will remove the asset
     * from the storage but not invalidate the slot at this index.
     *
     * @param id The asset identifier to remove.
     * @return void on success, or an error if the asset was not found.
     */
    std::expected<void, AssetError> remove(const AssetId<T>& id) {
        return std::visit(visitor{[this, &id](const AssetIndex& index) { return m_assets.remove(index); },
                                  [this, &id](const uuids::uuid& uuid) -> std::expected<void, AssetError> {
                                      if (contains(id)) {
                                          m_mapped_assets.erase(uuid);
                                          return {};
                                      } else {
                                          return std::unexpected(AssetNotPresent(uuid));
                                      }
                                  }},
                          id)
            .transform([this, &id]() { m_cached_events.emplace_back(AssetEvent<T>::removed(id)); });
    }

    /**
     * @brief Pop the asset at the given index. This will remove the asset from
     * the storage but not invalidate the slot at this index.
     *
     * @param id The asset identifier to take.
     * @return The taken asset on success, or an error if not found.
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
                m_cached_events.emplace_back(AssetEvent<T>::removed(id));
                return std::move(asset);
            });
    }

    /**
     * @brief Add an asset and return a strong handle to it.
     *  Convenience equivalent of emplace() matching bevy's Assets::add().
     *
     * @param asset The asset to add.
     * @return Handle<T> A strong handle to the new asset.
     */
    Handle<T> add(T asset) { return emplace(std::move(asset)); }

    /**
     * @brief Reserve a handle without inserting an asset yet.
     *  The handle can be used later to insert the asset.
     *
     * @return Handle<T> A strong handle with a reserved slot.
     */
    Handle<T> reserve_handle() {
        Handle<T> handle = m_handle_provider->reserve().template typed<T>();
        // Ensure storage is large enough
        auto index                 = std::get<AssetIndex>(handle.id());
        std::uint32_t storage_size = static_cast<std::uint32_t>(m_references.size());
        while (auto&& opt = m_handle_provider->index_allocator.reserved_receiver().try_receive()) {
            storage_size = std::max(storage_size, opt->index() + 1);
        }
        if (storage_size > m_references.size()) {
            m_assets.resize_slots(storage_size);
            m_references.resize(storage_size, 0);
        }
        m_references[index.index()] = 1;
        return handle;
    }

    /**
     * @brief Get an existing asset or insert one using a factory function.
     *
     * @param id The asset id.
     * @param insert_fn Factory called (no arguments) if the asset doesn't exist.
     * @return A mutable reference to the asset on success, or an error.
     */
    template <typename F>
        requires std::invocable<F> && std::constructible_from<T, std::invoke_result_t<F>>
    std::expected<std::reference_wrapper<T>, AssetError> get_or_insert_with(const AssetId<T>& id, F&& insert_fn) {
        if (contains(id)) {
            return try_get_mut(id);
        }
        return insert(id, std::invoke(std::forward<F>(insert_fn)))
            .and_then(
                [this, &id](bool) -> std::expected<std::reference_wrapper<T>, AssetError> { return try_get_mut(id); });
    }

    /**
     * @brief Get a mutable reference to an asset without recording a Modified event.
     *
     * @param id The asset identifier.
     * @return A mutable reference, or std::nullopt if not found.
     */
    std::optional<std::reference_wrapper<T>> get_mut_untracked(const AssetId<T>& id) {
        auto res =
            std::visit(visitor{[this](const AssetIndex& index) { return m_assets.try_get_mut(index); },
                               [this](const uuids::uuid& id) -> std::expected<std::reference_wrapper<T>, AssetError> {
                                   if (auto&& it = m_mapped_assets.find(id); it != m_mapped_assets.end()) {
                                       return std::ref(it->second);
                                   } else {
                                       return std::unexpected(AssetNotPresent(id));
                                   }
                               }},
                       id);
        return res.has_value() ? std::make_optional<std::reference_wrapper<T>>(res.value()) : std::nullopt;
    }

    /**
     * @brief Remove an asset without recording a Removed event.
     *
     * @param id The asset identifier.
     * @return The removed asset, or std::nullopt if not found.
     */
    std::optional<T> remove_untracked(const AssetId<T>& id) {
        auto res = std::visit(visitor{[this](const AssetIndex& index) { return m_assets.pop(index); },
                                      [this, &id](const uuids::uuid& uuid) -> std::expected<T, AssetError> {
                                          if (m_mapped_assets.contains(uuid)) {
                                              auto asset = std::move(m_mapped_assets.at(uuid));
                                              m_mapped_assets.erase(uuid);
                                              m_mapped_assets_ref.erase(uuid);
                                              return std::move(asset);
                                          } else {
                                              return std::unexpected(AssetNotPresent(uuid));
                                          }
                                      }},
                              id);
        return res.has_value() ? std::make_optional(std::move(res.value())) : std::nullopt;
    }

    /** @brief Check if the collection is empty. */
    bool is_empty() const { return m_assets.empty() && m_mapped_assets.empty(); }
    /** @brief Get the total number of assets stored. */
    std::size_t len() const { return m_assets.size() + m_mapped_assets.size(); }

    /**
     * @brief Collect all asset ids into a vector.
     *
     * @return A vector of all AssetId<T> in this collection.
     */
    std::vector<AssetId<T>> ids() const {
        std::vector<AssetId<T>> result;
        result.reserve(len());
        m_assets.for_each(
            [&](std::uint32_t idx, std::uint32_t gen, const T&) { result.emplace_back(AssetIndex(idx, gen)); });
        for (auto& [uuid, _] : m_mapped_assets) {
            result.emplace_back(uuid);
        }
        return result;
    }

    /**
     * @brief Iterate over all (id, const_ref) pairs.
     *
     * @param fn Callback invoked as fn(AssetId<T>, const T&) for each asset.
     */
    template <typename F>
    void iter(F&& fn) const {
        m_assets.for_each(
            [&](std::uint32_t idx, std::uint32_t gen, const T& asset) { fn(AssetId<T>(AssetIndex(idx, gen)), asset); });
        for (auto& [uuid, asset] : m_mapped_assets) {
            fn(AssetId<T>(uuid), asset);
        }
    }

    /**
     * @brief Iterate over all (id, mut_ref) pairs. Each visited asset generates a Modified event.
     *
     * @param fn Callback invoked as fn(AssetId<T>, T&) for each asset.
     */
    template <typename F>
    void iter_mut(F&& fn) {
        m_assets.for_each_mut([&](std::uint32_t idx, std::uint32_t gen, T& asset) {
            auto id = AssetId<T>(AssetIndex(idx, gen));
            m_cached_events.emplace_back(AssetEvent<T>::modified(id));
            fn(id, asset);
        });
        for (auto& [uuid, asset] : m_mapped_assets) {
            auto id = AssetId<T>(uuid);
            m_cached_events.emplace_back(AssetEvent<T>::modified(id));
            fn(id, asset);
        }
    }

    /** @brief Process pending handle destruction events manually. */
    void handle_events_manual(const AssetServer* asset_server = nullptr) {
        spdlog::trace("[{}] Handling events", meta::type_id<T>::short_name());
        while (auto&& opt = m_handle_provider->index_allocator.reserved_receiver().try_receive()) {
            m_assets.resize_slots(opt->index() + 1);
        }
        while (auto&& opt = m_handle_provider->event_receiver.try_receive()) {
            auto id = (*opt).id.template typed<T>();
            if ((*opt).loader_managed && asset_server) {
                if (!asset_server_process_handle_destruction(*asset_server, id)) {
                    continue;
                }
            }
            release(id);
        }
        spdlog::trace("[{}] Finished handling events", meta::type_id<T>::short_name());
    }

    /**
     * @brief Handle strong handle destruction events.
     */
    static void handle_events(core::ResMut<Assets<T>> assets, core::Res<AssetServer> asset_server) {
        assets->handle_events_manual(asset_server.ptr());
    }

    /** @brief System that flushes cached asset events to the event writer. */
    static void asset_events(core::ResMut<Assets<T>> assets, core::EventWriter<AssetEvent<T>> writer) {
        for (auto&& event : assets->m_cached_events) {
            writer.write(event);
        }
        assets->m_cached_events.clear();
    }
};
}  // namespace epix::assets