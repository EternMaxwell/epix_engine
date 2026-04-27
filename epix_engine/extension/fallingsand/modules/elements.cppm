module;
#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>
#endif

export module epix.extension.fallingsand:elements;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import glm;

namespace epix::ext::fallingsand {

/** @brief Spatial dimension constant for all falling-sand grids. */
export constexpr std::size_t kDim = 2;

export enum class ElementType {
    Solid,
    Powder,
    Liquid,
    Gas,
    Body,  ///< Coupled to a physics body (e.g. Box2D) — reserved for future use.
};

/**
 * @brief Shared properties for a kind of element (e.g. "sand", "water").
 *
 * Each unique element type is registered once in an ElementRegistry.
 * Individual cells reference a base element by its registry id.
 */
export struct ElementBase {
    std::string name;
    float density;
    ElementType type;
    /** @brief Procedural colour generator that maps a per-cell seed → RGBA. */
    std::function<glm::vec4(std::uint64_t seed)> color_func;
    // TODO: per-element step function interface (not yet implemented)
    // std::function<bool(class SandSimulation&, std::int64_t x, std::int64_t y)> step_func;
};

export enum class ElementRegistryError {
    NameAlreadyExists,
    NameNotFound,
    InvalidBaseId,
};

export struct ElementRegistry {
   private:
    std::vector<ElementBase> m_elements;
    std::unordered_map<std::string, std::size_t> m_name_to_id;

   public:
    std::expected<std::size_t, ElementRegistryError> register_element(ElementBase element) {
        if (m_name_to_id.contains(element.name)) return std::unexpected(ElementRegistryError::NameAlreadyExists);
        std::size_t id = m_elements.size();
        m_name_to_id.emplace(element.name, id);
        m_elements.emplace_back(std::move(element));
        return id;
    }

    std::expected<std::reference_wrapper<const ElementBase>, ElementRegistryError> get(std::size_t id) const {
        if (id >= m_elements.size()) return std::unexpected(ElementRegistryError::InvalidBaseId);
        return std::cref(m_elements[id]);
    }

    std::expected<std::size_t, ElementRegistryError> get_id(const std::string& name) const {
        auto it = m_name_to_id.find(name);
        if (it == m_name_to_id.end()) return std::unexpected(ElementRegistryError::NameNotFound);
        return it->second;
    }

    std::expected<std::reference_wrapper<const ElementBase>, ElementRegistryError> get(const std::string& name) const {
        auto id_res = get_id(name);
        if (!id_res.has_value()) return std::unexpected(id_res.error());
        return get(*id_res);
    }

    const ElementBase& operator[](std::size_t id) const { return m_elements[id]; }

    auto iter() const {
        return std::views::transform(m_name_to_id, [&elems = m_elements](const auto& pair) {
            return std::tuple<const std::string&, const ElementBase&>{pair.first, elems[pair.second]};
        });
    }
    auto iter_names() const { return std::views::keys(m_name_to_id); }
    auto iter_elements() const { return std::views::all(m_elements); }
    std::size_t size() const { return m_elements.size(); }
};

/** @brief Per-cell element data stored in a grid::Chunk layer. */
export struct Element {
    std::size_t base_id;
    glm::vec4 color;
};

}  // namespace epix::ext::fallingsand