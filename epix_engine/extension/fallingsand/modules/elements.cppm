module;
#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#endif

export module epix.extension.fallingsand:elements;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import glm;
import :temperature;

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

/** @brief Per-cell element data stored in a grid::Chunk layer.
 *
 * Thermal state (temperature, staging_heat, heat_emitted) lives in a
 * separate ThermalCell layer at the same world position; see ThermalCell.
 */
export struct Element {
    std::size_t base_id          = 0;
    glm::vec4 color              = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2 velocity           = {0.0f, 0.0f};  ///< Velocity in cells/s.
    glm::vec2 inpos              = {0.0f, 0.0f};  ///< Sub-cell fractional position.
    std::uint8_t flags           = 0;             ///< Status flags (FREEFALL | UPDATED).
    std::uint16_t not_move_count = 0;             ///< Ticks since last successful move.
    std::uint8_t transition_tag  = 0;             ///< Origin marker for transition filtering.

    static constexpr std::uint8_t kFreefall = 1 << 0;  ///< Cell is in free-fall.
    static constexpr std::uint8_t kUpdated  = 1 << 1;  ///< Cell was stepped this tick.
    static constexpr std::uint8_t kBurning  = 1 << 2;  ///< Cell is visibly burning / near ignition.

    bool freefall() const noexcept { return (flags & kFreefall) != 0; }
    void set_freefall(bool v) noexcept { flags = v ? flags | kFreefall : flags & ~kFreefall; }
    bool updated() const noexcept { return (flags & kUpdated) != 0; }
    void set_updated(bool v) noexcept { flags = v ? flags | kUpdated : flags & ~kUpdated; }
    bool burning() const noexcept { return (flags & kBurning) != 0; }
    void set_burning(bool v) noexcept { flags = v ? flags | kBurning : flags & ~kBurning; }
};

// ──────────────────────────────────────────────────────────────────────────────
// Action / Condition system (replaces old ElementTransition)
// ──────────────────────────────────────────────────────────────────────────────

// ── Conditions (all must be true for the action to fire) ────────────────────

/** @brief Element temperature >= target.
 *  If clamp=true, temperature is pinned at target and excess energy goes
 *  to staging_heat for phase transitions.  Default false (pure condition). */
export struct TemperatureAbove {
    float target;
    bool clamp = false;
};

/** @brief Element temperature <= target.
 *  If clamp=true, temperature is pinned at target and deficit energy goes
 *  to staging_heat for phase transitions.  Default false (pure condition). */
export struct TemperatureBelow {
    float target;
    bool clamp = false;
};

/** @brief Element has kBurning flag set. */
export struct IsBurning {};

/** @brief Element's transition_tag matches the given value. */
export struct HasTag {
    std::uint8_t tag;
};

/** @brief Any cardinal neighbour has the given element base_id. */
export struct ContactWith {
    std::size_t element_id;
};

/** @brief Staging heat is sufficient: staging >= latent_heat * density * cell_area
 *         (for TemperatureAbove) or staging <= -latent_heat * density * cell_area
 *         (for TemperatureBelow).  Used for phase transitions. */
export struct StagingHeat {
    float latent_heat_j_per_kg;
};

/** @brief Probabilistic condition evaluated during random tick.
 *         If probability > 0 it is used directly; otherwise compute_prob is called
 *         with the current Element to get the probability. */
export struct RandomTick {
    float probability = 0.0f;
    std::function<float(const Element&)> compute_prob;
    /** @brief Random-tick intensity for this action.  Higher = fewer cells picked. */
    int intensity = 1;
};

export using Condition =
    std::variant<TemperatureAbove, TemperatureBelow, IsBurning, HasTag, ContactWith, StagingHeat, RandomTick>;

// ── Actions ──────────────────────────────────────────────────────────────────

/** @brief Spawn particles of a given element in adjacent empty cells. */
export struct SpawnNearby {
    std::string element_name;  ///< Resolved to element_id at registration.
    std::size_t element_id = 0;
    int count_min          = 1;
    int count_max          = 1;
    std::uint8_t spawn_tag = 0;
};

/** @brief Transform into another element type. */
export struct TransformTo {
    std::string target_name;  ///< Resolved to target_id at registration.
    std::size_t target_id = 0;
};

/** @brief Clear the cell (element disappears). */
export struct Despawn {};

/** @brief Set kBurning flag (start exothermic reaction). */
export struct Ignite {};

/** @brief Clear kBurning flag (stop burning). */
export struct Extinguish {};

export using Action = std::variant<SpawnNearby, TransformTo, Despawn, Ignite, Extinguish>;

/** @brief A named behaviour: when all conditions are met, execute the action. */
export struct ElementAction {
    std::vector<Condition> conditions;
    Action action;
};

// ──────────────────────────────────────────────────────────────────────────────
// Element types
// ──────────────────────────────────────────────────────────────────────────────

/**
 * @brief Shared properties for a kind of element (e.g. "sand", "water").
 *
 * Each unique element type is registered once in an ElementRegistry.
 * Individual cells reference a base element by its registry id.
 */
export struct ElementBase {
    std::string name;
    float density     = 1.0f;
    ElementType type  = ElementType::Solid;
    float restitution = 0.1f;  ///< Collision elasticity (0 = inelastic, 1 = elastic).
    float friction    = 0.5f;  ///< Surface friction coefficient.
    float awake_rate  = 1.0f;  ///< Probability of waking when a neighbour moves (0..1).

    /** @brief Specific heat capacity in J/(kg·K). */
    float specific_heat = 800.0f;
    /** @brief Thermal conductivity in W/(m·K). */
    float thermal_conductivity = 1.0f;

    /** @brief Temperature at which the element visually appears to be burning / glowing (Kelvin).
     *  0 = never show burning. */
    float ignition_temperature = 0.0f;

    /** @brief Heat of combustion in J/kg.  Total energy released while burning.
     *  Used to compute the self-heating rate.  0 = no self-heating. */
    float heat_of_combustion = 0.0f;

    /** @brief Actions evaluated during heat transfer and random ticks. */
    std::vector<ElementAction> actions;

    /**
     * @brief Factory that builds a fully-initialised Element.
     *
     * @param seed    Per-cell seed for procedural variation.
     * @param base_id The registry id assigned to this element type.
     * @param base    Reference to this ElementBase (access to density, etc.).
     */
    using ConstructFunc          = std::function<Element(std::size_t, const ElementBase&, std::uint64_t)>;
    ConstructFunc construct_func = [](std::size_t id, const ElementBase&, std::uint64_t) {
        return Element{id, glm::vec4(1.0f)};
    };

    /** @brief Factory that builds the ThermalCell written to the thermal layer
     *  when this element is spawned via set_cell / put_cell.  Defaults to a
     *  freshly-constructed `ThermalCell{}` (293 K).  Override per-element for
     *  hot/cold spawns (e.g. lava, ice) or to inject env-dependent variation.
     *
     *  @param base_id The registry id assigned to this element type.
     *  @param base    Reference to this ElementBase.
     *  @param seed    Per-cell seed for procedural variation.
     */
    using ThermalConstructFunc = std::function<ThermalCell(std::size_t, const ElementBase&, std::uint64_t)>;
    ThermalConstructFunc thermal_construct_func = [](std::size_t, const ElementBase&, std::uint64_t) {
        return ThermalCell{};
    };
};

// ──────────────────────────────────────────────────────────────────────────────
// ElementBaseBuilder — fluent API for constructing ElementBase
// ──────────────────────────────────────────────────────────────────────────────

export class ElementBaseBuilder {
   public:
    class ActionBuilder {
       public:
        ActionBuilder& temp_above(float t, bool clamp = false) {
            m_act.conditions.push_back(TemperatureAbove{t, clamp});
            return *this;
        }
        ActionBuilder& temp_below(float t, bool clamp = false) {
            m_act.conditions.push_back(TemperatureBelow{t, clamp});
            return *this;
        }
        ActionBuilder& is_burning() {
            m_act.conditions.push_back(IsBurning{});
            return *this;
        }
        ActionBuilder& has_tag(std::uint8_t t) {
            m_act.conditions.push_back(HasTag{t});
            return *this;
        }
        ActionBuilder& contact_with(std::size_t id) {
            m_act.conditions.push_back(ContactWith{id});
            return *this;
        }
        ActionBuilder& staging_heat(float v) {
            m_act.conditions.push_back(StagingHeat{v});
            return *this;
        }
        ActionBuilder& random_tick(float prob, int intensity = 1) {
            m_act.conditions.push_back(RandomTick{prob, {}, intensity});
            return *this;
        }
        ActionBuilder& random_tick_fn(std::function<float(const Element&)> fn, int intensity = 1) {
            m_act.conditions.push_back(RandomTick{0.0f, std::move(fn), intensity});
            return *this;
        }

        ElementBaseBuilder& ignite() {
            m_act.action = Ignite{};
            return finish();
        }
        ElementBaseBuilder& extinguish() {
            m_act.action = Extinguish{};
            return finish();
        }
        ElementBaseBuilder& despawn() {
            m_act.action = Despawn{};
            return finish();
        }
        ElementBaseBuilder& transform_to(std::string name) {
            m_act.action = TransformTo{std::move(name), 0};
            return finish();
        }
        ElementBaseBuilder& spawn_nearby(std::string name, int min, int max, std::uint8_t tag = 0) {
            m_act.action = SpawnNearby{std::move(name), 0, min, max, tag};
            return finish();
        }

       private:
        friend class ElementBaseBuilder;
        ActionBuilder(ElementBaseBuilder& owner) : m_owner(owner) {}
        ElementBaseBuilder& finish() {
            m_owner.m_base.actions.push_back(std::move(m_act));
            return m_owner;
        }
        ElementAction m_act;
        ElementBaseBuilder& m_owner;
    };

    explicit ElementBaseBuilder(std::string name) { m_base.name = std::move(name); }
    ElementBaseBuilder& type(ElementType t) {
        m_base.type = t;
        return *this;
    }
    ElementBaseBuilder& density(float v) {
        m_base.density = v;
        return *this;
    }
    ElementBaseBuilder& restitution(float v) {
        m_base.restitution = v;
        return *this;
    }
    ElementBaseBuilder& friction(float v) {
        m_base.friction = v;
        return *this;
    }
    ElementBaseBuilder& awake_rate(float v) {
        m_base.awake_rate = v;
        return *this;
    }
    ElementBaseBuilder& specific_heat(float v) {
        m_base.specific_heat = v;
        return *this;
    }
    ElementBaseBuilder& thermal_conductivity(float v) {
        m_base.thermal_conductivity = v;
        return *this;
    }
    ElementBaseBuilder& ignition_temperature(float v) {
        m_base.ignition_temperature = v;
        return *this;
    }
    ElementBaseBuilder& heat_of_combustion(float v) {
        m_base.heat_of_combustion = v;
        return *this;
    }
    ElementBaseBuilder& construct_func(ElementBase::ConstructFunc f) {
        m_base.construct_func = std::move(f);
        return *this;
    }
    ElementBaseBuilder& thermal_construct_func(ElementBase::ThermalConstructFunc f) {
        m_base.thermal_construct_func = std::move(f);
        return *this;
    }

    ActionBuilder add_action() { return ActionBuilder(*this); }
    ElementBase build() && { return std::move(m_base); }

   private:
    ElementBase m_base;
};

export enum class ElementRegistryError {
    NameAlreadyExists,
    NameNotFound,
    InvalidBaseId,
    UnresolvedTransitionTarget,
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

    /**
     * @brief Resolve all transition target_name / spawn_name strings to ids.
     *
     * Call after all elements are registered.  Returns the first unresolved
     * name encountered, or void on success.
     */
    std::expected<void, ElementRegistryError> resolve_all_references() {
        for (auto& base : m_elements) {
            for (auto& act : base.actions) {
                std::visit(
                    [&](auto& action) {
                        using T = std::decay_t<decltype(action)>;
                        if constexpr (std::is_same_v<T, TransformTo>) {
                            if (!action.target_name.empty()) {
                                auto id_res = get_id(action.target_name);
                                if (id_res.has_value()) action.target_id = *id_res;
                            }
                        } else if constexpr (std::is_same_v<T, SpawnNearby>) {
                            if (!action.element_name.empty()) {
                                auto id_res = get_id(action.element_name);
                                if (id_res.has_value()) action.element_id = *id_res;
                            }
                        }
                    },
                    act.action);
            }
        }
        return {};
    }

    std::expected<std::reference_wrapper<const ElementBase>, ElementRegistryError> get(std::size_t id) const noexcept {
        if (id >= m_elements.size()) return std::unexpected(ElementRegistryError::InvalidBaseId);
        return std::cref(m_elements[id]);
    }

    std::expected<std::reference_wrapper<ElementBase>, ElementRegistryError> get_mut(std::size_t id) noexcept {
        if (id >= m_elements.size()) return std::unexpected(ElementRegistryError::InvalidBaseId);
        return std::ref(m_elements[id]);
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

    const ElementBase& operator[](std::size_t id) const noexcept { return m_elements[id]; }

    auto iter() const {
        return std::views::transform(m_name_to_id, [&elems = m_elements](const auto& pair) {
            return std::tuple<const std::string&, const ElementBase&>{pair.first, elems[pair.second]};
        });
    }
    auto iter_names() const { return std::views::keys(m_name_to_id); }
    auto iter_elements() const { return std::views::all(m_elements); }
    std::size_t size() const noexcept { return m_elements.size(); }
};

}  // namespace epix::ext::fallingsand