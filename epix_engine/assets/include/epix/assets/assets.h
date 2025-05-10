#pragma once

#include <epix/app.h>

#include <concepts>

#include "handle.h"
#include "index.h"

namespace epix::assets {
template <typename T>
struct Entry {
    std::optional<T> asset = std::nullopt;
    uint32_t generation    = 0;
};

template <typename T>
struct AssetStorage {
   private:
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
    std::optional<bool> insert(const AssetIndex& index, Args&&... args) {
        if (index.index >= m_storage.size()) {
            return std::nullopt;
        }
        if (!m_storage[index.index]) {
            m_storage[index.index]             = Entry<T>();
            m_storage[index.index]->generation = index.generation;
        } else if (m_storage[index.index]->generation != index.generation) {
            return std::nullopt;
        }
        bool res = m_storage[index.index]->asset.has_value();
        m_storage[index.index]->asset =
            std::make_optional<T>(std::forward<Args>(args)...);
        m_size++;
        return res;
    }

    /**
     * @brief Check if the asset at the given index is valid.
     *
     * This means that the index is within bounds, the slot at this index is
     * available, and the generation matches.
     *
     * @param index The index to check.
     * @return True if the asset is valid, false otherwise.
     */
    bool valid(const AssetIndex& index) const {
        return index.index < m_storage.size() && m_storage[index.index] &&
               m_storage[index.index]->generation == index.generation;
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
        return index.index < m_storage.size() && m_storage[index.index] &&
               m_storage[index.index]->asset.has_value() &&
               m_storage[index.index]->generation == index.generation;
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
    std::optional<T> pop(const AssetIndex& index) {
        if (contains(index)) {
            auto asset = std::move(m_storage[index.index]->asset.value());
            m_storage[index.index]->asset = std::nullopt;
            m_size--;
            return std::move(asset);
        } else {
            return std::nullopt;
        }
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
    bool remove(const AssetIndex& index) {
        if (index.index < m_storage.size() && m_storage[index.index] &&
            m_storage[index.index]->generation == index.generation) {
            m_storage[index.index]->asset = std::nullopt;
            m_size--;
            return true;
        } else {
            return false;
        }
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
    bool remove_dereferenced(const AssetIndex& index) {
        if (index.index < m_storage.size() && m_storage[index.index] &&
            m_storage[index.index]->generation == index.generation) {
            m_storage[index.index] = std::nullopt;
            m_size--;
            return true;
        } else {
            return false;
        }
    }

    T* get(const AssetIndex& index) {
        if (contains(index)) {
            return &m_storage[index.index]->asset.value();
        } else {
            return nullptr;
        }
    }

    const T* get(const AssetIndex& index) const {
        if (contains(index)) {
            return &m_storage[index.index]->asset.value();
        } else {
            return nullptr;
        }
    }
};

template <typename T>
struct AssetEvent {
    enum class Type {
        Added,     // Asset added
        Removed,   // Asset removed
        Modified,  // Asset modified or replaced
        Unused,    // All strong handles destroyed
    } type;
    AssetId<T> id;

    static AssetEvent<T> added(const AssetId<T>& id) {
        return {Type::Added, id};
    }
    static AssetEvent<T> removed(const AssetId<T>& id) {
        return {Type::Removed, id};
    }
    static AssetEvent<T> modified(const AssetId<T>& id) {
        return {Type::Modified, id};
    }
    static AssetEvent<T> unused(const AssetId<T>& id) {
        return {Type::Unused, id};
    }
};

template <typename T>
    requires std::move_constructible<T> && std::is_move_assignable_v<T>
struct Assets {
   private:
    AssetStorage<T> m_assets;
    std::vector<uint32_t> m_references;
    std::shared_ptr<HandleProvider> m_handle_provider;
    entt::dense_map<uuids::uuid, T> m_mapped_assets;
    entt::dense_map<uuids::uuid, uint32_t> m_mapped_assets_ref;
    std::shared_ptr<spdlog::logger> m_logger;
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
    std::optional<bool> insert_index(const AssetIndex& index, Args&&... args) {
        while (auto&& opt =
                   m_handle_provider->index_allocator.reserved_receiver()
                       .try_receive()) {
            m_assets.resize_slots(opt->index);
            m_references.resize(opt->index + 1, 0);
        }
        return m_assets.insert(index, std::forward<Args>(args)...);
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
    bool insert_uuid(const uuids::uuid& id, Args&&... args) {
        if (contains(AssetId<T>(id))) {
            m_logger->debug("Replacing asset at Uuid:{}", uuids::to_string(id));
            auto& asset = m_mapped_assets.at(id);
            asset.~T();
            new (&asset) T(std::forward<Args>(args)...);
            return true;
        }
        m_logger->debug("Inserting asset at Uuid:{}", uuids::to_string(id));
        m_mapped_assets.emplace(id, std::forward<Args>(args)...);
        m_mapped_assets_ref.emplace(id, 0);
        return false;
    }

   public:
    Assets() : m_handle_provider(std::make_shared<HandleProvider>(typeid(T))) {
        m_logger =
            spdlog::default_logger()->clone(typeid(decltype(*this)).name());
    }
    Assets(const Assets&)            = delete;
    Assets(Assets&&)                 = delete;
    Assets& operator=(const Assets&) = delete;
    Assets& operator=(Assets&&)      = delete;

    void set_log_level(spdlog::level::level_enum level) {
        m_logger->set_level(level);
    }
    void set_log_label(const std::string& label) {
        m_logger = m_logger->clone(label);
    }

    /**
     * @brief Get the handle provider for this assets collection.
     *
     * @return `std::shared_ptr<HandleProvider<T>>` The handle provider for this
     */
    std::shared_ptr<HandleProvider> get_handle_provider() {
        return m_handle_provider;
    }

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
        std::visit(
            epix::util::visitor{
                [this](const AssetIndex& index) {
                    m_logger->debug(
                        "Emplacing asset at {} with gen {}", index.index,
                        index.generation
                    );
                },
                [this](const uuids::uuid& id) {
                    m_logger->debug(
                        "Emplacing asset at Uuid:{}", uuids::to_string(id)
                    );
                }
            },
            handle.id()
        );
        auto res = insert(handle, std::forward<Args>(args)...);
        std::visit(
            epix::util::visitor{
                [this](const AssetIndex& index) {
                    m_references[index.index]++;
                },
                [this](const auto& id) {}
            },
            handle.id()
        );
        if (!res) {
            m_logger->error(
                "Unable to emplace new value at the index: index not valid(gen "
                "mismatch or no asset slot at given index)"
            );
        } else if (res.value()) {
            m_logger->debug("Replacing asset");
        } else {
            m_logger->debug("Inserting asset");
        }
        return handle;
    }

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::optional<bool> insert(const AssetId<T>& id, Args&&... args) {
        auto res = std::visit(
            epix::util::visitor{
                [this, &args...](const AssetIndex& index) {
                    return insert_index(index, std::forward<Args>(args)...);
                },
                [this, &args...](const uuids::uuid& id) -> std::optional<bool> {
                    return insert_uuid(id, std::forward<Args>(args)...);
                }
            },
            id
        );
        if (res) {
            if (res.value()) {
                m_cached_events.emplace_back(AssetEvent<T>::modified(id));
            } else {
                m_cached_events.emplace_back(AssetEvent<T>::added(id));
                m_cached_events.emplace_back(AssetEvent<T>::modified(id));
            }
        }
        return res;
    }

    /**
     * @brief Check if there is an asset at the given index.
     *
     * This is used internally.
     */
    bool contains(const AssetId<T>& id) const {
        return std::visit(
            epix::util::visitor{
                [this](const AssetIndex& index) {
                    return m_assets.contains(index);
                },
                [this](const uuids::uuid& id) {
                    return m_mapped_assets.contains(id) &&
                           m_mapped_assets_ref.contains(id);
                }
            },
            id
        );
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
    std::optional<Handle<T>> get_strong_handle(const AssetId<T>& id) {
        if (!contains(id)) return std::nullopt;
        std::visit(
            epix::util::visitor{
                [this](const AssetIndex& index) {
                    m_references[index.index]++;
                },
                [this](const uuids::uuid& id) { m_mapped_assets_ref.at(id)++; }
            },
            id
        );
        return m_handle_provider->get_handle(id, false, std::nullopt);
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
    const T* get(const AssetId<T>& id) const {
        return std::visit(
            epix::util::visitor{
                [this](const AssetIndex& index) { return m_assets.get(index); },
                [this](const uuids::uuid& id) -> const T* {
                    if (auto&& it = m_mapped_assets.find(id);
                        it != m_mapped_assets.end()) {
                        return &it->second;
                    } else {
                        return nullptr;
                    }
                }
            },
            id
        );
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
    T* get_mut(const AssetId<T>& id) {
        auto res = std::visit(
            epix::util::visitor{
                [this](const AssetIndex& index) { return m_assets.get(index); },
                [this](const uuids::uuid& id) -> T* {
                    if (auto&& it = m_mapped_assets.find(id);
                        it != m_mapped_assets.end()) {
                        return &it->second;
                    } else {
                        return nullptr;
                    }
                }
            },
            id
        );
        if (res) {
            m_cached_events.emplace_back(AssetEvent<T>::modified(id));
        }
        return res;
    }

    /**
     * @brief Remove the asset at the given index. This will remove the asset
     * from the storage but not invalidate the slot at this index.
     *
     * @param index The index of the asset to remove.
     * @return `bool` True if the operation was successful, false otherwise.
     */
    bool remove(const AssetId<T>& id) {
        auto res = std::visit(
            epix::util::visitor{
                [this, &id](const AssetIndex& index) {
                    if (contains(id)) {
                        m_logger->trace(
                            "Force removing asset at {} with gen {}, current "
                            "ref count is "
                            "{}",
                            index.index, index.generation,
                            m_references[index.index]
                        );
                        return m_assets.remove(index);
                    } else {
                        return false;
                    }
                },
                [this, &id](const uuids::uuid& uuid) {
                    if (contains(id)) {
                        m_logger->trace(
                            "Force removing asset at Uuid:{}",
                            uuids::to_string(uuid)
                        );
                        m_mapped_assets.erase(uuid);
                        m_mapped_assets_ref.erase(uuid);
                        return true;
                    } else {
                        return false;
                    }
                }
            },
            id
        );
        if (res) {
            m_cached_events.emplace_back(AssetEvent<T>::removed(id));
        }
        return res;
    }

    bool release_index(const AssetIndex& index) {
        m_logger->trace(
            "Releasing asset at {} with gen {}, current ref count is {}",
            index.index, index.generation, m_references[index.index]
        );
        if (contains(AssetId<T>(index))) {
            m_references[index.index]--;
            if (m_references[index.index] == 0) {
                m_cached_events.emplace_back(
                    AssetEvent<T>::unused(AssetId<T>(index))
                );
                m_assets.remove_dereferenced(index);
                m_handle_provider->index_allocator.release(index);
                m_cached_events.emplace_back(
                    AssetEvent<T>::removed(AssetId<T>(index))
                );
                return true;
            }
        }
        return false;
    }
    bool release_uuid(const uuids::uuid& id) {
        if (contains(AssetId<T>(id))) {
            m_logger->trace(
                "Releasing asset at Uuid:{}, current ref count is {}",
                uuids::to_string(id), m_mapped_assets_ref.at(id)
            );
            auto& ref = m_mapped_assets_ref.at(id);
            ref--;
            if (ref == 0) {
                m_cached_events.emplace_back(AssetEvent<T>::unused(AssetId<T>(id
                )));
                m_mapped_assets.erase(id);
                m_mapped_assets_ref.erase(id);
                m_cached_events.emplace_back(
                    AssetEvent<T>::removed(AssetId<T>(id))
                );
                return true;
            }
        }
        return false;
    }
    bool release(const AssetId<T>& id) {
        return std::visit(
            epix::util::visitor{
                [this](const AssetIndex& index) {
                    return release_index(index);
                },
                [this](const uuids::uuid& id) { return release_uuid(id); }
            },
            id
        );
    }

    /**
     * @brief Pop the asset at the given index. This will remove the asset from
     * the storage but not invalidate the slot at this index.
     *
     * @param index The index of the asset to pop.
     * @return `std::optional<T>` The asset at the given index, or std::nullopt
     * if the asset is not valid.
     */
    std::optional<T> pop_index(const AssetIndex& index) {
        if (contains(index)) {
            m_logger->trace(
                "Force popping asset at {} with gen {}, current ref count is "
                "{}",
                index.index, index.generation, m_references[index.index]
            );
            m_cached_events.emplace_back(AssetEvent<T>::removed(AssetId<T>(index
            )));
            return std::move(m_assets.pop(index));
        } else {
            return std::nullopt;
        }
    }
    std::optional<T> pop_uuid(const uuids::uuid& id) {
        if (contains(id)) {
            m_logger->trace(
                "Force popping asset at Uuid:{}", uuids::to_string(id)
            );
            auto asset = std::move(m_mapped_assets.at(id));
            m_mapped_assets.erase(id);
            m_mapped_assets_ref.erase(id);
            m_cached_events.emplace_back(AssetEvent<T>::removed(id));
            return std::move(asset);
        } else {
            return std::nullopt;
        }
    }
    std::optional<T> pop(const AssetId<T>& id) {
        return std::visit(
            epix::util::visitor{
                [this](const AssetIndex& index) { return pop_index(index); },
                [this](const uuids::uuid& id) { return pop_uuid(id); }
            },
            id
        );
    }

    /**
     * @brief Handle strong handle destruction events.
     */
    void handle_events() {
        m_logger->trace("Handling events");
        while (auto&& opt =
                   m_handle_provider->index_allocator.reserved_receiver()
                       .try_receive()) {
            m_assets.resize_slots(opt->index);
        }
        while (auto&& opt = m_handle_provider->event_receiver.try_receive()) {
            auto&& [id] = *opt;
            release(id.typed<T>());
        }
        m_logger->trace("Finished handling events");
    }

    static void res_handle_events(epix::ResMut<Assets<T>> assets) {
        assets->handle_events();
    }
    static void asset_events(
        epix::ResMut<Assets<T>> assets, epix::EventWriter<AssetEvent<T>> writer
    ) {
        for (auto&& event : assets->m_cached_events) {
            writer.write(event);
        }
        assets->m_cached_events.clear();
    }
};
}  // namespace epix::assets