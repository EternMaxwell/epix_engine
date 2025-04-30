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

    std::optional<std::reference_wrapper<T>> get(const AssetIndex& index) {
        if (contains(index)) {
            return std::make_optional<std::reference_wrapper<T>>(
                m_storage[index.index]->asset.value()
            );
        } else {
            return std::nullopt;
        }
    }

    std::optional<std::reference_wrapper<const T>> get(const AssetIndex& index
    ) const {
        if (contains(index)) {
            return std::make_optional<std::reference_wrapper<const T>>(
                m_storage[index.index]->asset.value()
            );
        } else {
            return std::nullopt;
        }
    }
};

template <typename T>
    requires std::move_constructible<T> && std::is_move_assignable_v<T>
struct Assets {
   private:
    AssetStorage<T> m_assets;
    std::shared_ptr<HandleProvider<T>> m_handle_provider;
    std::function<void(T&)> m_destruct_behaviour;
    std::shared_ptr<spdlog::logger> m_logger;

   public:
    Assets() : m_handle_provider(std::make_shared<HandleProvider<T>>()) {
        m_logger =
            spdlog::default_logger()->clone(typeid(decltype(*this)).name());
    }
    Assets(const Assets&)            = delete;
    Assets(Assets&&)                 = delete;
    Assets& operator=(const Assets&) = delete;
    Assets& operator=(Assets&&)      = delete;

    /**
     * @brief Set the callback when an asset is released(has no strong
     * references).
     *
     * @param behaviour The callback to call when an asset is released.
     */
    void set_destruct_behaviour(std::function<void(T&&)> behaviour) {
        m_destruct_behaviour = behaviour;
    }
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
    std::shared_ptr<HandleProvider<T>> get_handle_provider() {
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
        Handle<T> handle = m_handle_provider->reserve();
        while (auto&& opt = m_handle_provider->m_reserved.try_receive()) {
            m_assets.resize_slots(opt->index);
        }
        AssetIndex index = handle;
        m_logger->trace(
            "Emplacing asset at {} with gen {}", index.index, index.generation
        );
        auto res = m_assets.insert(index, std::forward<Args>(args)...);
        if (!res) {
            m_logger->error(
                "Failed to emplace asset at {} with gen {}, generation "
                "mismatch",
                index.index, index.generation
            );
        } else if (res.value()) {
            m_logger->debug(
                "Replaced asset at {} with gen {}", index.index,
                index.generation
            );
        } else {
            m_logger->debug(
                "Inserted asset at {} with gen {}", index.index,
                index.generation
            );
        }
        return handle;
    }

    /**
     * @brief Insert an asset at the given index.
     *
     * @return `std::optional<bool>` True if the asset was replaced, false if
     * new value was inserted. `std::nullopt` if the index is invalid
     * (generation mismatch or no asset slot at given index).
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::optional<bool> insert(const AssetIndex& index, Args&&... args) {
        while (auto&& opt = m_handle_provider->m_reserved.try_receive()) {
            m_assets.resize_slots(opt->index);
        }
        return m_assets.insert(index, std::forward<Args>(args)...);
    }

    /**
     * @brief Check if there is an asset at the given index.
     *
     * This is used internally.
     */
    bool contains(const AssetIndex& index) const {
        return m_assets.contains(index);
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
    std::optional<Handle<T>> get_strong_handle(const AssetIndex& index) {
        if (contains(index)) {
            m_handle_provider->reference(index.index);
            return std::make_optional<Handle<T>>(std::make_shared<StrongHandle>(
                index.index, index.generation,
                m_handle_provider->m_event_receiver.create_sender()
            ));
        } else {
            return std::nullopt;
        }
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
    std::optional<std::reference_wrapper<const T>> get(const AssetIndex& index
    ) const {
        return m_assets.get(index);
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
    std::optional<std::reference_wrapper<T>> get_mut(const AssetIndex& index) {
        return m_assets.get(index);
    }

    /**
     * @brief Remove the asset at the given index. This will remove the asset
     * from the storage but not invalidate the slot at this index.
     *
     * @param index The index of the asset to remove.
     * @return `bool` True if the operation was successful, false otherwise.
     */
    bool remove(const AssetIndex& index) {
        if (contains(index)) {
            m_logger->trace(
                "Force removing asset at {} with gen {}, current ref count is "
                "{}",
                index.index, index.generation,
                m_handle_provider->ref_count(index.index)
            );
            return m_assets.remove(index);
        } else {
            return false;
        }
    }

    /**
     * @brief Pop the asset at the given index. This will remove the asset from
     * the storage but not invalidate the slot at this index.
     *
     * @param index The index of the asset to pop.
     * @return `std::optional<T>` The asset at the given index, or std::nullopt
     * if the asset is not valid.
     */
    std::optional<T> pop(const AssetIndex& index) {
        if (contains(index)) {
            m_logger->trace(
                "Force popping asset at {} with gen {}, current ref count is "
                "{}",
                index.index, index.generation,
                m_handle_provider->ref_count(index.index)
            );
            return std::move(m_assets.pop(index));
        } else {
            return std::nullopt;
        }
    }

    /**
     * @brief Handle strong handle destruction events.
     */
    void handle_events() {
        m_logger->trace("Handling events");
        while (auto&& opt = m_handle_provider->m_reserved.try_receive()) {
            m_assets.resize_slots(opt->index);
        }
        m_handle_provider->handle_events([this](const AssetIndex& index) {
            // this index now has 0 references, we can destroy the asset
            m_logger->debug(
                "Asset at {} with gen {} has 0 references, destroying it",
                index.index, index.generation
            );
            auto asset = m_assets.get(index);
            if (asset) {
                m_logger->debug(
                    "Destroying asset at {} with gen {}", index.index,
                    index.generation
                );
                if (m_destruct_behaviour) {
                    m_destruct_behaviour(asset.value().get());
                }
            } else {
                m_logger->error(
                    "Failed to destroy asset at {} with gen {}, asset not "
                    "found",
                    index.index, index.generation
                );
            }
            m_assets.remove_dereferenced(index);
        });
        m_logger->trace("Finished handling events");
    }

    static void res_handle_events(epix::ResMut<Assets<T>> assets) {
        assets->handle_events();
    }
};
}  // namespace epix::assets