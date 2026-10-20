#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/render/alpha.hpp>
#include <epix/render/erased_render_asset.hpp>
#include <epix/camera.hpp>
#include <epix/mesh.hpp>
#include <cstdint>
#include <epix/assets/id.hpp>
// `MaterialPlugin` calls `app_register_asset`, which this declares. Without it the
// header only compiled in translation units that already had it transitively, so a
// first-time user of `MaterialPlugin` - an example, in this case - failed to build.
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/ecs/core/tick.hpp>
#include <epix/render/sync_world.hpp>
#include <epix/shader.hpp>
#include <expected>
#include <memory>
#include <optional>
#include <typeinfo>
#include <unordered_map>
#endif

#include <epix/pbr/mesh_pipeline_resource.hpp>
#include <epix/pbr/light_markers.hpp>
#include <epix/pbr/mesh_pipeline_key.hpp>
#include <epix/pbr/mesh_material.hpp>
#include <epix/pbr/material_bindings.hpp>
#include <epix/pbr/material_bind_groups.hpp>
#include <epix/pbr/prepass.hpp>

namespace epix::pbr {

/** @brief The material a single mesh instance uses (Bevy
 *  `RenderMaterialInstance`, `material.rs:598-607`).
 *
 *  Untyped on purpose: the render world's instance records are not generic over
 *  the material type, so one map holds every mesh's material whatever it is. */
EPIX_EXPORT struct RenderMaterialInstance {
    /** @brief The material asset. */
    assets::UntypedAssetId asset_id;
    /** @brief The change tick at which this record was last modified, compared
     *  against `RenderMaterialInstances::current_change_tick` when sweeping. */
    ecs::Tick last_change_tick;
};

/** @brief The material a mesh with no material is treated as having (Bevy
 *  `DUMMY_MESH_MATERIAL`, `material.rs:429-430`).
 *
 *  Bevy's constant is typed `AssetId<StandardMaterial>` and only converted to an
 *  untyped id where it is used; `StandardMaterial` is not ported, so the sentinel
 *  is untyped here and stands for "no material at all". Meshes almost always have
 *  materials - the exceptions are custom pipelines that draw without one - so the
 *  sentinel is what keeps those from being mistaken for a real asset. */
EPIX_EXPORT inline const assets::UntypedAssetId DUMMY_MESH_MATERIAL = assets::UntypedAssetId::invalid();

/** @brief Every mesh's material in the render world (Bevy
 *  `RenderMaterialInstances`, `material.rs:571-596`).
 *
 *  `mesh_material` is the accessor the GPU instance builder uses: a mesh with no
 *  record falls back to `DUMMY_MESH_MATERIAL` rather than failing, which is how a
 *  mesh drawn by a custom pipeline still gets a binding. */
EPIX_EXPORT struct EPIX_API RenderMaterialInstances {
    /** @brief The material record of each entity that has one. */
    render::sync_world::MainEntityHashMap<RenderMaterialInstance> instances;
    /** @brief A monotonically increasing counter used to sweep `instances` when
     *  entities or their material components are removed. */
    ecs::Tick current_change_tick;

    /** @brief The material id for an entity, or the dummy id when it has none
     *  (Bevy `RenderMaterialInstances::mesh_material`, `material.rs:590-595`). */
    [[nodiscard]] assets::UntypedAssetId mesh_material(render::sync_world::MainEntity entity) const;
};

/** @brief Which bind group slot each material asset was bound at (Bevy
 *  `RenderMaterialBindings`, `material.rs:1602`).
 *
 *  Bevy's is a newtype over the map so that it is a distinct resource; the map is
 *  the whole of it either way. The builder reads it to turn a mesh's material into
 *  the slot its bind group was written at. */
EPIX_EXPORT struct EPIX_API RenderMaterialBindings {
    std::unordered_map<assets::UntypedAssetId, MaterialBindingId> bindings;

    /** @brief The binding id a material asset was bound at, if it was prepared
     *  (Bevy's `RenderMaterialBindings::get`). */
    [[nodiscard]] std::optional<MaterialBindingId> get(const assets::UntypedAssetId& asset_id) const;
};


/** @brief The meshes whose material or visibility changed (Bevy
 *  `Or<(Changed<ViewVisibility>, Changed<MeshMaterial3d<M>>)>`, `material.rs:703`). */
template <typename M>
using ChangedMeshMaterialFilter = ecs::Or<ecs::Modified<camera::ViewVisibility>, ecs::Modified<MeshMaterial3d<M>>>;

/** @brief Fills @ref RenderMaterialInstances from the scene's meshes (Bevy
 *  `extract_mesh_materials`, `bevy_pbr/src/material.rs:698-718`).
 *
 *  A **visible** mesh records which material it uses and the tick it changed at;
 *  an invisible one has its record removed here rather than in the sweep, because
 *  becoming invisible is exactly what this query observes. */
template <typename M>
void extract_mesh_materials(ecs::ResMut<RenderMaterialInstances> material_instances,
                            app::Extract<ecs::Query<ecs::Item<ecs::Entity, const camera::ViewVisibility&,
                                                                 const MeshMaterial3d<M>&>,
                                                    ChangedMeshMaterialFilter<M>>> changed_meshes_query) {
    const ecs::Tick last_change_tick = material_instances->current_change_tick;

    for (auto&& [entity, view_visibility, material] : changed_meshes_query.iter()) {
        const render::sync_world::MainEntity main_entity{entity};
        if (view_visibility.get()) {
            // `insert_or_assign` rather than `operator[]`: the record's untyped asset
            // id has no default, so the map cannot default-construct one.
            material_instances->instances.insert_or_assign(
                main_entity, RenderMaterialInstance{.asset_id = assets::UntypedAssetId{material.id()},
                                                    .last_change_tick = last_change_tick});
        } else {
            material_instances->instances.erase(main_entity);
        }
    }
}

/** @brief The first of two sweep phases: drops the records of meshes whose
 *  `MeshMaterial3d<M>` was removed (Bevy `early_sweep_material_instances`,
 *  `material.rs:743-763`).
 *
 *  A material of one type replaced by a material of another in the same frame is
 *  common, and the removed-component event still fires for the old type - so an
 *  entry updated *this* frame is left alone, which is what the tick comparison
 *  checks. Running once per material type is why the tick is bumped by the second
 *  phase instead. */
template <typename M>
void early_sweep_material_instances(ecs::ResMut<RenderMaterialInstances> material_instances,
                                    app::Extract<ecs::RemovedComponents<MeshMaterial3d<M>>> removed_materials_query) {
    const ecs::Tick last_change_tick = material_instances->current_change_tick;

    for (const ecs::Entity entity : removed_materials_query.read()) {
        const render::sync_world::MainEntity main_entity{entity};
        const auto found = material_instances->instances.find(main_entity);
        if (found != material_instances->instances.end() && found->second.last_change_tick != last_change_tick) {
            material_instances->instances.erase(found);
        }
    }
}


/** @brief The second sweep phase: drops the records of meshes whose `Mesh3d` was
 *  removed, then advances the registry's change tick for the next frame (Bevy
 *  `late_sweep_material_instances`, `material.rs:772-800`).
 *
 *  It runs after every `early_sweep_material_instances`, which is what lets the
 *  tick be bumped exactly once per frame. */
EPIX_EXPORT EPIX_API void late_sweep_material_instances(
    ecs::ResMut<RenderMaterialInstances> material_instances,
    app::Extract<ecs::RemovedComponents<mesh::Mesh3d>> removed_meshes_query);


/** @brief The material's AsBindGroup associated data (Bevy `M::Data`).
 *  Empty bind-group specializations explicitly use std::monostate. */
template <typename M>
struct MaterialData {
    using type = typename render::render_resource::AsBindGroup<M>::Data;
};

/** @brief The bind group a 3D material is bound at (Bevy `MATERIAL_BIND_GROUP_INDEX`,
 *  `material.rs:66`).
 *
 *  It is 3, not 2: the mesh pipeline's layout is `[view, view binding arrays, mesh]`, so the
 *  mesh uniform owns group 2 - `mesh_bindings.wgsl:7` declares it there and
 *  `SetMeshBindGroup<2>` binds it - and the material's layout is inserted after it. The two
 *  have to agree, or the material's layout displaces the mesh's and the mesh pipeline fails
 *  to create with `group 2, binding 0 is not available in the pipeline layout`. */
inline constexpr std::uint32_t MATERIAL_BIND_GROUP_INDEX = 3;

/** @brief Whether an opaque or mask material goes through the forward or the
 *  deferred renderer (Bevy `OpaqueRendererMethod`, `material.rs:157-163`).
 *
 *  `Auto` defers to the `DefaultOpaqueRendererMethod` resource, which is part of
 *  the material plugin and therefore lands with it. */
EPIX_EXPORT enum class OpaqueRendererMethod {
    Forward,  /**< Draw it in the forward pass. */
    Deferred, /**< Draw it in the deferred pass. */
    Auto,     /**< Follow the `DefaultOpaqueRendererMethod` resource. */
};

/** @brief The render method an `OpaqueRendererMethod::Auto` material resolves to
 *  (Bevy `DefaultOpaqueRendererMethod`, `bevy_pbr/src/material.rs:1330-1351`).
 *
 *  A main-world resource that `PbrPlugin` initialises and extracts into the render
 *  world with `ExtractResourcePlugin<DefaultOpaqueRendererMethod>` (Bevy
 *  `bevy_pbr/src/lib.rs:220,224`). `Default` is Bevy's `#[derive(Default)]`, which
 *  leaves the method at `OpaqueRendererMethod::Forward`. */
EPIX_EXPORT struct DefaultOpaqueRendererMethod {
   private:
    /** @brief The method `Auto` resolves to. Bevy's private tuple field. */
    OpaqueRendererMethod m_method = OpaqueRendererMethod::Forward;

   public:
    /** @brief The forward renderer (Bevy `DefaultOpaqueRendererMethod::forward`). */
    static DefaultOpaqueRendererMethod forward() noexcept { return DefaultOpaqueRendererMethod{}; }

    /** @brief The deferred renderer (Bevy `DefaultOpaqueRendererMethod::deferred`). */
    static DefaultOpaqueRendererMethod deferred() noexcept {
        DefaultOpaqueRendererMethod method;
        method.set_to_deferred();
        return method;
    }

    /** @brief Switch to the forward renderer (Bevy `set_to_forward`). */
    void set_to_forward() noexcept { m_method = OpaqueRendererMethod::Forward; }

    /** @brief Switch to the deferred renderer (Bevy `set_to_deferred`). */
    void set_to_deferred() noexcept { m_method = OpaqueRendererMethod::Deferred; }

    /** @brief The method `Auto` resolves to. Bevy's field is public (its tuple member);
     *  this class keeps it private, so it needs the reader as well as the writers. */
    [[nodiscard]] OpaqueRendererMethod method() const noexcept { return m_method; }
};

/** @brief The specialization key of one 3D material (Bevy `MaterialPipelineKey`,
 *  `material.rs:427-435`). The prepared pipeline is a separate argument to
 *  Material::specialize, not part of this key. */
EPIX_EXPORT template <typename M>
struct MaterialPipelineKey {
    /** @brief The mesh half of the key. */
    MeshPipelineKey mesh_key{};
    /** @brief The material's own bind-group data (Bevy `M::Data`). */
    typename MaterialData<M>::type bind_group_data{};

    bool operator==(const MaterialPipelineKey&) const = default;
};

namespace detail {
/** @brief The erased key's payload interface (Bevy `ErasedMaterialKeyVTable`,
 *  `material.rs:1438-1444`), as the base class C++ uses in place of a vtable
 *  value. Bevy stores a clone function and a partial-equality function; the
 *  virtual members are the same two operations. */
struct ErasedMaterialKeyConcept {
    virtual ~ErasedMaterialKeyConcept()                                          = default;
    virtual std::unique_ptr<const ErasedMaterialKeyConcept> clone() const        = 0;
    virtual bool equals(const ErasedMaterialKeyConcept& other) const             = 0;
};

template <typename T>
struct ErasedMaterialKeyModel final : ErasedMaterialKeyConcept {
    T value;
    explicit ErasedMaterialKeyModel(T material_key) : value(std::move(material_key)) {}
    std::unique_ptr<const ErasedMaterialKeyConcept> clone() const override {
        return std::make_unique<ErasedMaterialKeyModel>(value);
    }
    bool equals(const ErasedMaterialKeyConcept& other) const override {
        const auto* typed = dynamic_cast<const ErasedMaterialKeyModel*>(&other);
        return typed != nullptr && value == typed->value;
    }
};
}  // namespace detail

/** @brief A material's bind-group key with its type erased (Bevy
 *  `ErasedMaterialKey`, `material.rs:1430-1445`), which is what lets one
 *  specialization cache serve every material type.
 *
 *  Bevy keeps the key's `TypeId`, a precomputed `FixedHasher` hash and the boxed
 *  value; this holds the same three. Each key owns its payload, and copying a
 *  key clones that payload through the erased dispatch, as Bevy's Clone does. */
EPIX_EXPORT class ErasedMaterialKey {
   public:
    /** @brief Erase one material's bind-group data, which must be cloneable,
     *  hashable and comparable (Bevy's `T: Clone + Hash + PartialEq`). */
    template <typename T>
        requires std::copy_constructible<T> && std::equality_comparable<T>
    explicit ErasedMaterialKey(T material_key)
        : m_type_id(meta::type_id<T>()),
          m_hash(std::hash<T>{}(material_key)),
          m_value(std::make_unique<detail::ErasedMaterialKeyModel<T>>(std::move(material_key))) {}

    /** @brief Bevy's default erased key contains the unit type. */
    ErasedMaterialKey() : ErasedMaterialKey(std::monostate{}) {}

    ErasedMaterialKey(const ErasedMaterialKey& other)
        : m_type_id(other.m_type_id), m_hash(other.m_hash), m_value(other.m_value->clone()) {}

    ErasedMaterialKey& operator=(const ErasedMaterialKey& other) {
        if (this != &other) {
            // Clone first: a throwing user copy must not partially replace the key.
            auto value = other.m_value->clone();
            m_type_id = other.m_type_id;
            m_hash = other.m_hash;
            m_value = std::move(value);
        }
        return *this;
    }

    ErasedMaterialKey(ErasedMaterialKey&&) noexcept = default;
    ErasedMaterialKey& operator=(ErasedMaterialKey&&) noexcept = default;

    /** @brief Clone the typed key (Bevy `to_key`). A wrong type is a contract
     *  violation: Bevy's downcast panics; C++ reports std::bad_cast. */
    template <typename T>
    T to_key() const {
        const auto* typed = dynamic_cast<const detail::ErasedMaterialKeyModel<T>*>(m_value.get());
        if (typed == nullptr) throw std::bad_cast{};
        return typed->value;
    }

    /** @brief Bevy's PartialEq implementation compares type, then left and right payloads. */
    bool operator==(const ErasedMaterialKey& other) const {
        return m_type_id == other.m_type_id && m_value->equals(*other.m_value);
    }

    /** @brief Bevy's `FixedHasher::hash_one` over the same key. */
    std::size_t hash() const noexcept { return m_hash; }

    /** @brief The erased payload's type (Bevy's `type_id` field). */
    const meta::type_index& type_id() const noexcept { return m_type_id; }

   private:
    meta::type_index m_type_id{};
    std::size_t m_hash = 0;
    std::unique_ptr<const detail::ErasedMaterialKeyConcept> m_value;
};

/** @brief The specialization key of a material whose data is erased (Bevy
 *  `ErasedMaterialPipelineKey`, `material.rs:438-443`): the mesh half, the erased
 *  material bind-group key, and the material type itself.
 *
 *  Bevy holds both a `material_key` and a `type_id`, which are not redundant: the
 *  first identifies the *bind-group data* a material family specializes on, the
 *  second the material type it belongs to. Equality and hashing include both;
 *  C++ field initializers additionally allow aggregate/resource construction. */
EPIX_EXPORT struct ErasedMaterialPipelineKey {
    /** @brief The mesh half of the key. */
    MeshPipelineKey mesh_key{};
    /** @brief The material's bind-group data, with its type erased. */
    ErasedMaterialKey material_key;
    /** @brief The material type this key belongs to (Bevy's `type_id`). */
    meta::type_index type_id{};

    bool operator==(const ErasedMaterialPipelineKey&) const = default;
};

EPIX_EXPORT struct MaterialPipeline;

/** @brief A 3D material (Bevy `Material`, `bevy_pbr/src/material.rs:138-273`).  A
 *  material type derives from this and overrides only what it needs; the pipeline
 *  calls the derived members through static dispatch.
 *
 *  Shader hooks retain Bevy's Default/Handle/Path alternatives through ShaderRef.
 *  Paths are resolved by AssetServer without changing their asset source.
 *
 *  The meshlet shaders (`meshlet_mesh_fragment_shader` and its two siblings) are
 *  absent: meshlets are a Bevy subsystem this port does not have. */
EPIX_EXPORT template <typename M>
struct Material : render::render_resource::AsBindGroupBase {
    /** @brief This material's vertex shader, or the default mesh vertex shader. */
    static shader::ShaderRef vertex_shader() { return {}; }
    /** @brief This material's fragment shader, or the default mesh fragment shader. */
    static shader::ShaderRef fragment_shader() { return {}; }
    /** @brief This material's alpha mode (Bevy `alpha_mode`, which defaults to
     *  `AlphaMode::Opaque`). */
    render::AlphaMode alpha_mode() const { return render::AlphaMode::opaque(); }
    /** @brief Which renderer draws this material (Bevy `opaque_render_method`,
     *  which defaults to `Forward`). */
    OpaqueRendererMethod opaque_render_method() const { return OpaqueRendererMethod::Forward; }
    /** @brief A view-depth bias forcing a specific render order (Bevy
     *  `depth_bias`). */
    float depth_bias() const { return 0.0f; }
    /** @brief Whether this material reads the view transmission texture (Bevy
     *  `reads_view_transmission_texture`). */
    bool reads_view_transmission_texture() const { return false; }
    /** @brief Whether the prepass is enabled for this material. Bevy's default is
     *  `true`, which is easy to get wrong in either direction. */
    static bool enable_prepass() { return true; }
    /** @brief Whether shadows are enabled for this material (Bevy `enable_shadows`,
     *  default `true`). */
    static bool enable_shadows() { return true; }
    /** @brief This material's prepass vertex shader, also used for shadows. */
    static shader::ShaderRef prepass_vertex_shader() { return {}; }
    /** @brief This material's prepass fragment shader, also used for shadows. */
    static shader::ShaderRef prepass_fragment_shader() { return {}; }
    /** @brief This material's deferred vertex shader. */
    static shader::ShaderRef deferred_vertex_shader() { return {}; }
    /** @brief This material's deferred fragment shader. */
    static shader::ShaderRef deferred_fragment_shader() { return {}; }
    /** @brief Customize the specialized pipeline and propagate errors (Bevy
     *  `Material::specialize`, `material.rs:265-272`). */
    static std::expected<void, render::SpecializedMeshPipelineError> specialize(
        const MaterialPipeline&, render::RenderPipelineDescriptor&,
        const mesh::MeshVertexBufferLayoutRef&, MaterialPipelineKey<M>) { return {}; }
};

/** @brief Whether this material ends up using bindless resources (Bevy
 *  `material_uses_bindless_resources`, `bevy_pbr/src/material_bind_groups.rs:1681-1687`):
 *  it declares a slab size, the device supports the arrays, and a slab is worth having.
 *
 *  The `> 1` guard is Bevy's and it matters: a material whose slabs would hold a single
 *  slot is drawn non-bindless instead, because a bindless allocation costs a whole bind
 *  group for one material. The first test short-circuits, which is why a material that never
 *  opted in is never asked whether the device could support it. */
template <typename M>
    requires std::derived_from<M, Material<M>>
[[nodiscard]] bool material_uses_bindless_resources(const wgpu::Device& render_device) {
    const std::optional<render::render_resource::BindlessSlabResourceLimit> slot_count = M::bindless_slot_count();
    return slot_count.has_value() && M::bindless_supported(render_device) && slot_count->resolve() > 1;
}

/** @brief Register the bind-group allocator for one material type (Bevy
 *  `add_material_bind_group_allocator`, `material.rs:406-422`), run once at
 *  `RenderStartup` before anything allocates against it.
 *
 *  Bevy's constraint is just `M: Material`, because its derive guarantees the rest; C++
 *  asks for the pieces explicitly - the material base, the `AsBindGroup<C>` specialization
 *  that supplies the label and the layout entries, and the bindless members the allocator
 *  reads. Bevy passes a derived `bind_group_layout_descriptor(device)`; the port builds the
 *  descriptor from the specialization's `label()` and `layout_entries()`, which is what the
 *  derive produces them from too.
 *
 *  The bindless descriptor is handed over only when the material actually ends up bindless;
 *  otherwise the allocator is the non-bindless one and never looks at it. */
template <typename M>
    requires std::derived_from<M, Material<M>> && render::render_resource::AsBindGroupImpl<M>
void add_material_bind_group_allocator(ecs::Commands,
                                       ecs::Res<wgpu::Device> render_device,
                                       ecs::ResMut<MaterialBindGroupAllocators> bind_group_allocators) {
    const wgpu::Device& device = render_device.get();
    const std::string_view label  = render::render_resource::AsBindGroup<M>::label();

    render::BindGroupLayoutDescriptor bind_group_layout =
        render::BindGroupLayoutDescriptor()
            .set_label(std::string(label))
            .set_entries(render::render_resource::AsBindGroup<M>::layout_entries());

    const bool bindless = material_uses_bindless_resources<M>(device);
    bind_group_allocators->insert(
        meta::type_id<M>(),
        MaterialBindGroupAllocator::make(device, label,
                                         bindless ? M::bindless_descriptor() : std::nullopt,
                                         std::move(bind_group_layout), M::bindless_slot_count()));
}


/** @brief The extraction systems that record specialization ticks, which the sweep
 *  then consumes (Bevy `MaterialExtractEntitiesNeedingSpecializationSystems`,
 *  `material.rs:392`). */
EPIX_EXPORT enum class MaterialExtractEntitiesNeedingSpecializationSystems {
    Extract, /**< The per-material pass that records specialization ticks. */
};
/** @brief The extraction systems of one material type, which the sweeps order
 *  around (Bevy `MaterialExtractionSystems`, `material.rs:396`). */
EPIX_EXPORT enum class MaterialExtractionSystems {
    Extract, /**< The per-material extraction pass this ordering set marks. */
};

/** @brief Entities whose material changed and that need re-specialization (Bevy
 *  `EntitiesNeedingSpecialization<M>`, `material.rs:596-610`), the 3D counterpart
 *  of the 2D resource of the same name. */
EPIX_EXPORT template <typename M>
struct EntitiesNeedingSpecialization {
    std::vector<ecs::Entity> entities;
};

/** @brief Ticks saying when an entity's pipeline was last specialized (Bevy
 *  `EntitySpecializationTickPair`, `material.rs:944-952`). Bevy keeps **two**
 *  ticks for a subtle reason: a specialization can be invalidated either by the
 *  system's own tick moving or by the material registry's, and the two are
 *  advanced by different systems. */
EPIX_EXPORT struct EntitySpecializationTickPair {
    /** @brief The standard system tick. */
    ecs::Tick system_tick;
    /** @brief The `RenderMaterialInstances` tick, advanced by
     *  `late_sweep_material_instances`. */
    ecs::Tick material_instances_tick;
};

/** @brief When each entity's pipeline was last specialized (Bevy
 *  `EntitySpecializationTicks`, `material.rs:904-908`). */
EPIX_EXPORT struct EntitySpecializationTicks {
    render::sync_world::MainEntityHashMap<EntitySpecializationTickPair> entities;
};

/** @brief Records, for every entity the main world queued, when its pipeline was
 *  last specialized (Bevy `extract_entities_needs_specialization<M>`,
 *  `material.rs:786-805`).
 *
 *  Both ticks are stored because either can invalidate a specialization: the
 *  system tick moves with the frame, and the registry tick moves when a material
 *  instance is swept. */
EPIX_EXPORT template <typename M>
void extract_entities_needs_specialization(
    app::Extract<ecs::Res<EntitiesNeedingSpecialization<M>>> entities_needing_specialization,
    ecs::ResMut<EntitySpecializationTicks> entity_specialization_ticks,
    ecs::Res<RenderMaterialInstances> render_material_instances,
    ecs::SystemChangeTick ticks) {
    for (const ecs::Entity entity : entities_needing_specialization->entities) {
        entity_specialization_ticks->entities.insert_or_assign(
            render::sync_world::MainEntity{entity},
            EntitySpecializationTickPair{.system_tick = ticks.this_run(),
                                        .material_instances_tick = render_material_instances->current_change_tick});
    }
}

/** @brief Queues the entities whose mesh or material changed so they are
 *  re-specialized (Bevy `check_entities_needing_specialization<M>`,
 *  `bevy_pbr/src/material.rs:969-990`), the 3D counterpart of the 2D system of the
 *  same name. Bevy fills its list from a parallel local; the result is the same
 *  collection. */
EPIX_EXPORT template <typename M>
void check_entities_needing_specialization(
    ecs::Query<ecs::Item<ecs::Entity>,
               ecs::Filter<ecs::Or<ecs::Modified<mesh::Mesh3d>, assets::AssetChanged<mesh::Mesh3d>,
                                   ecs::Modified<MeshMaterial3d<M>>, assets::AssetChanged<MeshMaterial3d<M>>>,
                           ecs::With<MeshMaterial3d<M>>>>
        needs_specialization,
    ecs::ResMut<EntitiesNeedingSpecialization<M>> entities_needing_specialization) {
    entities_needing_specialization->entities.clear();
    for (auto&& [entity] : needs_specialization.iter()) {
        entities_needing_specialization->entities.push_back(entity);
    }
}

/** @brief Queues the light entities whose `NotShadowCaster` changed (Bevy
 *  `check_light_entities_needing_specialization<M>`,
 *  `bevy_pbr/src/render/light.rs:1686-1705`).
 *
 *  Bevy defines it beside the lights rather than beside the materials; the port
 *  keeps it here, next to the resource it fills. Only a removed `NotShadowCaster`
 *  whose entity still exists needs re-specialization - a removal whose entity is
 *  gone has nothing left to specialize. */
EPIX_EXPORT template <typename M>
void check_light_entities_needing_specialization(
    ecs::Query<ecs::Item<ecs::Entity>,
               ecs::Filter<ecs::With<MeshMaterial3d<M>>, ecs::Modified<NotShadowCaster>>>
        needs_specialization,
    ecs::Query<ecs::Item<ecs::Entity>, ecs::Filter<ecs::With<MeshMaterial3d<M>>>> mesh_materials,
    ecs::ResMut<EntitiesNeedingSpecialization<M>> entities_needing_specialization,
    ecs::RemovedComponents<NotShadowCaster> removed_components) {
    for (auto&& [entity] : needs_specialization.iter()) {
        entities_needing_specialization->entities.push_back(entity);
    }
    for (const ecs::Entity removed : removed_components.read()) {
        if (mesh_materials.get(removed).has_value()) {
            entities_needing_specialization->entities.push_back(removed);
        }
    }
}


/** @brief The cached pipeline of each material entity in one view (Bevy
 *  `SpecializedMaterialViewPipelineCache`, `material.rs:954-960`): material entity
 *  to the tick it was specialized at and the pipeline it got. */
EPIX_EXPORT struct SpecializedMaterialViewPipelineCache {
    render::sync_world::MainEntityHashMap<std::pair<ecs::Tick, render::CachedRenderPipelineId>> map;
};

/** @brief The per-view caches of specialized material pipelines (Bevy
 *  `SpecializedMaterialPipelineCache`, `material.rs:948-954`). */
EPIX_EXPORT struct SpecializedMaterialPipelineCache {
    std::unordered_map<render::view::RetainedViewEntity, SpecializedMaterialViewPipelineCache> map;
};

/** @brief Drops the specialization bookkeeping of entities whose
 *  `MeshMaterial3d<M>` was removed (Bevy `sweep_entities_needing_specialization<M>`,
 *  `bevy_pbr/src/material.rs:820-880`).
 *
 *  An entity whose tick was **updated this frame** is kept, not swept: a material
 *  replaced by one of another type in the same frame appears in the removed
 *  components list while also having been re-added, which is why this runs after
 *  `extract_entities_needs_specialization` and reads the tick it recorded.
 *
 *  **Bevy also clears the prepass and shadow pipeline caches here**, through two
 *  optional resources. Neither cache exists in the port - they belong to the prepass
 *  and shadow material features - so only the main cache is cleared, and the rows
 *  for those features carry the rest. */
EPIX_EXPORT template <typename M>
void sweep_entities_needing_specialization(
    ecs::ResMut<EntitySpecializationTicks> entity_specialization_ticks,
    app::Extract<ecs::RemovedComponents<MeshMaterial3d<M>>> removed_mesh_material_components,
    ecs::ResMut<SpecializedMaterialPipelineCache> specialized_material_pipeline_cache,
    ecs::Res<RenderMaterialInstances> render_material_instances,
    ecs::Query<ecs::Item<const render::view::ExtractedView&>> views) {
    for (const ecs::Entity entity : removed_mesh_material_components.read()) {
        const render::sync_world::MainEntity main_entity{entity};

        // A tick updated this frame means the entity changed materials rather than
        // losing one - keep its bookkeeping.
        const auto ticks = entity_specialization_ticks->entities.find(main_entity);
        if (ticks != entity_specialization_ticks->entities.end() &&
            ticks->second.material_instances_tick == render_material_instances->current_change_tick) {
            continue;
        }

        entity_specialization_ticks->entities.erase(main_entity);
        for (auto&& [view] : views.iter()) {
            const auto cache = specialized_material_pipeline_cache->map.find(view.retained_view_entity);
            if (cache != specialized_material_pipeline_cache->map.end()) {
                cache->second.map.erase(main_entity);
            }
        }
    }
}


/** @brief The 3D mesh pipeline data a material specializes against (Bevy
 *  `MaterialPipeline`, `bevy_pbr/src/material.rs:442-447`). */
EPIX_EXPORT struct MaterialPipeline {
    /** @brief The mesh pipeline this material's pipeline derives from. */
    MeshPipeline mesh_pipeline;
};

/** @brief Insert the `MaterialPipeline` resource once the mesh pipeline exists
 *  (Bevy `init_material_pipeline`, `material.rs:506-510`, registered in
 *  `RenderStartup` at `material.rs:300`). Everything that specializes a 3D
 *  material against the mesh pipeline reads this resource, so it is the first
 *  piece of the system half that turns a material into a drawable pipeline. */
EPIX_EXPORT EPIX_API void init_material_pipeline(ecs::Commands commands, ecs::Res<MeshPipeline> mesh_pipeline);

/** @brief Create and recreate any bind groups holding materials that were modified,
 *  by asking every registered allocator to prepare its own (Bevy
 *  `prepare_material_bind_groups`, `material.rs:1907-1922`).
 *
 *  Bevy walks the registry through its `Deref`; here the map is a member, and the
 *  dispatch takes the union of what its two arms need - the queue for the non-bindless
 *  arm's uniform buffers, the fallback resources and image for the bindless one. */
EPIX_EXPORT EPIX_API void prepare_material_bind_groups(ecs::ResMut<MaterialBindGroupAllocators> allocators,
                                                       ecs::Res<wgpu::Device> render_device,
                                                       ecs::Res<wgpu::Queue> render_queue,
                                                       ecs::Res<render::PipelineServer> pipeline_server,
                                                       ecs::Res<render::texture::FallbackImage> fallback_image,
                                                       ecs::Res<FallbackBindlessResources> fallback_resources);

/** @brief Upload every buffer the allocators manage (Bevy
 *  `write_material_bind_group_buffers`, `material.rs:1929-1937`).
 *
 *  Only the bindless arm owns buffers - the non-bindless one keeps its data in uniform
 *  buffers it writes itself - so this is a no-op for a non-bindless material type. */
EPIX_EXPORT EPIX_API void write_material_bind_group_buffers(ecs::ResMut<MaterialBindGroupAllocators> allocators,
                                                            ecs::Res<wgpu::Device> render_device,
                                                            ecs::Res<wgpu::Queue> render_queue);

/** @brief Bevy `MaterialsPlugin` (`material.rs:276-335`), the once-per-app plugin that
 *  owns everything every material type shares, added by `PbrPlugin` (`lib.rs:225`).
 *
 *  Bevy also registers here the material draw commands, `specialize_material_meshes`,
 *  `queue_material_meshes`, the shadow family and the three light caches. The port
 *  registers the draw commands from the plugins that own their phases - an equivalence
 *  recorded against PBR-67 and PBR-69 - and has none of the systems or caches the rest
 *  need, so what this carries is the shared registries, the material pipeline resource
 *  and the pair that builds then uploads the allocators' bind groups. */
EPIX_EXPORT struct EPIX_API MaterialsPlugin {
    void attach(epix::app::App& app);
};

// `MaterialPlugin` and the 3D material's `ErasedRenderAsset` specialization live in
// `epix/pbr/material_plugin.hpp`: they resolve the phase draw-function ids by name, and those
// sequences are declared in `mesh_instance.hpp`.


/** @brief Marks a mesh as changed when the material it renders with changed (Bevy
 *  `mark_meshes_as_changed_if_their_materials_changed<M>`, `material.rs:679-691`).
 *
 *  A material swap has to reach `Mesh3d`'s change tick, because that is what the
 *  extraction queries watch; `AssetChanged` is what catches the common case where
 *  the component is untouched and only the asset's contents changed. Both terms are
 *  needed - the component for a swap, the asset for an edit. */
EPIX_EXPORT template <typename M>
void mark_meshes_as_changed_if_their_materials_changed(
    ecs::Query<ecs::Item<ecs::Entity, ecs::Mut<mesh::Mesh3d>>,
               ecs::Filter<ecs::Or<ecs::Modified<MeshMaterial3d<M>>,
                                   assets::AssetChanged<MeshMaterial3d<M>>>>> changed_meshes) {
    for (auto&& [entity, mesh] : changed_meshes.iter()) {
        (void)entity;
        // Bevy writes `mesh.set_changed()`. This port's `Mut` marks modified on
        // mutable access and has no `set_changed`, so `ptr_mut()` is the accessor
        // that marks without a semantic write - the same effect.
        (void)mesh.ptr_mut();
    }
}



/** @brief Which render phase a material is drawn in (Bevy `RenderPhaseType`,
 *  `bevy_pbr/src/material.rs:1588-1600`). Not to be confused with the render
 *  module's `BinnedRenderPhaseType`, which is a different enum of the same family. */
EPIX_EXPORT enum class RenderPhaseType {
    Opaque,       /**< Drawn in the opaque phase. */
    AlphaMask,    /**< Drawn in the alpha-mask phase. */
    Transmissive, /**< Drawn in the transmissive phase. */
    Transparent,  /**< Drawn in the transparent phase. */
};

/** @brief The vertex shader a material supplies (Bevy `MaterialVertexShader`,
 *  `material.rs:1381`). Empty, as Bevy's is: the port's label machinery turns an
 *  empty type into a label. */
EPIX_EXPORT struct MaterialVertexShader {};
/** @brief The fragment shader a material supplies (Bevy `MaterialFragmentShader`,
 *  `material.rs:1384`). */
EPIX_EXPORT struct MaterialFragmentShader {};
/** @brief Shader override labels for the prepass and deferred pipelines (Bevy
 *  `material.rs:1387-1396`). */
EPIX_EXPORT struct PrepassVertexShader {};
EPIX_EXPORT struct PrepassFragmentShader {};
EPIX_EXPORT struct DeferredVertexShader {};
EPIX_EXPORT struct DeferredFragmentShader {};

/** @brief Everything the pipeline needs to know about a prepared material (Bevy
 *  `MaterialProperties`, `material.rs:1510-1585`).
 *
 *  Bevy's `shaders` and `draw_functions` are `SmallVec`s sized for
 *  `StandardMaterial`'s most common cases; the port uses vectors, since the size
 *  is a performance detail rather than a behaviour. */
/** @brief The draw-function labels a material's properties are keyed by (Bevy
 *  `material.rs:1408-1427`).
 *
 *  Bevy resolves each of these to the id its phase registered the draw sequence under, at
 *  preparation time, and stores the pair in `MaterialProperties::draw_functions`; the queue
 *  then asks for one by label. `ShadowsDrawFunction` is declared but never filled: the shadow
 *  family is unported (PBR-72). */
EPIX_EXPORT struct MainPassOpaqueDrawFunction {};
EPIX_EXPORT struct MainPassAlphaMaskDrawFunction {};
EPIX_EXPORT struct MainPassTransmissiveDrawFunction {};
EPIX_EXPORT struct MainPassTransparentDrawFunction {};
EPIX_EXPORT struct PrepassOpaqueDrawFunction {};
EPIX_EXPORT struct PrepassAlphaMaskDrawFunction {};
EPIX_EXPORT struct DeferredOpaqueDrawFunction {};
EPIX_EXPORT struct DeferredAlphaMaskDrawFunction {};
EPIX_EXPORT struct ShadowsDrawFunction {};

EPIX_EXPORT struct MaterialProperties {
    /** @brief Which renderer draws this material. */
    OpaqueRendererMethod render_method = OpaqueRendererMethod::Forward;
    /** @brief The material's alpha mode. */
    render::AlphaMode alpha_mode = render::AlphaMode::opaque();
    /** @brief The mesh pipeline key bits for this material, precalculated so the
     *  queue can simply or them together. */
    MeshPipelineKey mesh_pipeline_key_bits{};
    /** @brief A view-depth bias forcing a specific render order. */
    float depth_bias = 0.0f;
    /** @brief Whether the material reads the view transmission texture. */
    bool reads_view_transmission_texture = false;
    /** @brief The phase this material is drawn in. */
    RenderPhaseType render_phase_type = RenderPhaseType::Opaque;
    /** @brief The material's own bind-group data, type-erased (Bevy
     *  `MaterialProperties::material_key`, `material.rs:441`).
     *
     *  It is what lets one specialization cache serve every material type: two materials
     *  of the same type whose data differs must not share a compiled pipeline.
     *  Preparation fills it from AsBindGroup::bind_group_data; empty bind-group
     *  specializations use monostate for their associated Data. */
    ErasedMaterialKey material_key{std::monostate{}};
    /** @brief The material's bind-group layout, when it has one of its own. */
    std::optional<render::BindGroupLayoutDescriptor> material_layout;
    /** @brief The draw functions this material's phases use. */
    std::vector<std::pair<render::phase::InternedDrawFunctionLabel, render::phase::DrawFunctionId>> draw_functions;
    /** @brief The custom shaders this material supplies. */
    std::vector<std::pair<render::phase::InternedShaderLabel, assets::Handle<shader::Shader>>> shaders;
    /** @brief Whether this material actually uses bindless resources, once platform
     *  support is taken into account. */
    bool bindless = false;
    /** @brief The typed material hook, erased when its material is prepared. */
    using Specialize = std::expected<void, render::SpecializedMeshPipelineError> (*)(
        const MaterialPipeline&, render::RenderPipelineDescriptor&,
        const mesh::MeshVertexBufferLayoutRef&, const ErasedMaterialPipelineKey&);
    Specialize specialize = nullptr;
    /** @brief Whether the material's shadows are enabled (Bevy `enable_shadows`, read once
     *  while preparing). */
    bool shadows_enabled = true;
    /** @brief Whether the material's prepass is enabled (Bevy `enable_prepass`, read once
     *  while preparing). */
    bool prepass_enabled = true;

    /** @brief The shader this material supplies under the given label, or null
     *  (Bevy `MaterialProperties::get_shader`, `material.rs:1478-1490`). Bevy returns
     *  `Option<&Handle<Shader>>`; this port spells an optional reference as a
     *  pointer. The list is keyed by interned shader label on both sides. */
    template <typename Label>
    const assets::Handle<shader::Shader>* get_shader() const noexcept {
        const render::phase::InternedShaderLabel wanted =
            ecs::intern(render::phase::ShaderLabel{Label{}});
        for (const auto& [label, shader] : shaders) {
            if (label == wanted) return &shader;
        }
        return nullptr;
    }

    /** @brief The draw function this material registered under @p Label, or nothing (Bevy
     *  `MaterialProperties::get_draw_function`, `material.rs:1570-1576`).
     *
     *  Bevy's takes the label as a value and this takes it as a type parameter, matching
     *  `get_shader` above: the port keys both lists by their interned label. A material's
     *  properties carry one entry per phase the phase plugin registered, so a miss means the
     *  phase has no sequence and the queue skips the mesh rather than drawing it unbound. */
    template <typename Label>
    std::optional<render::phase::DrawFunctionId> get_draw_function() const noexcept {
        const render::phase::InternedDrawFunctionLabel wanted =
            ecs::intern(render::phase::DrawFunctionLabel{Label{}});
        for (const auto& [label, draw_function] : draw_functions) {
            if (label == wanted) return draw_function;
        }
        return std::nullopt;
    }
};

/** @brief Specializes a 3D material's pipeline against the mesh pipeline, with the
 *  material type erased (Bevy `MaterialPipelineSpecializer`, `material.rs:451-504`).
 *
 *  Bevy holds the material pipeline and the prepared properties in an `Arc`; this
 *  holds the pipeline and a `shared_ptr`, as `PreparedMaterial` does. Bevy declares
 *  this before `MaterialProperties` and Rust does not care; C++ does, so it follows
 *  them here. */
EPIX_EXPORT struct MaterialPipelineSpecializer {
    /** @brief The key this specializer is cached under (Bevy's associated `Key`). */
    using Key = ErasedMaterialPipelineKey;

    /** @brief The mesh pipeline data the material specializes against. */
    MaterialPipeline pipeline;
    /** @brief What the pipeline needs to know about the prepared material. */
    std::shared_ptr<MaterialProperties> properties;

    /** @brief Specialize the mesh pipeline for this material (Bevy
     *  `SpecializedMeshPipeline for MaterialPipelineSpecializer`): push the material
     *  group's shader def and its layout, apply shader overrides and the material
     *  callback, then add `BINDLESS` when the material is bindless. */
    EPIX_API std::expected<render::RenderPipelineDescriptor, render::SpecializedMeshPipelineError> specialize(
        const Key& key, const mesh::MeshVertexBufferLayoutRef& layout) const;
};

/** @brief A material whose GPU data is ready (Bevy `PreparedMaterial`,
 *  `material.rs:1605-1610`): where its data lives, and what the pipeline needs to
 *  know. Bevy holds the properties in an `Arc`; this is a `shared_ptr`. */
EPIX_EXPORT struct PreparedMaterial {
    /** @brief The bind group and slot the material's data lives in. */
    MaterialBindingId binding{};
    /** @brief What the pipeline needs to know about it. */
    std::shared_ptr<MaterialProperties> properties;
};



}  // namespace epix::pbr

// The 3D material's `ErasedRenderAsset` specialization lives in `epix/pbr/material_plugin.hpp`
// beside the plugin that installs it; see the note there.


namespace epix::pbr {

/** @brief Binds one material's bind group at slot `I` (Bevy `SetMaterialBindGroup<I>`,
 *  `material.rs:522-569`).
 *
 *  Note what this command does *not* take: there is no material type parameter in Bevy, and
 *  the group is not read from the prepared material either. A prepared material holds only a
 *  binding id, so the group is reached through the per-type allocator's slab, looked up by the
 *  instance's asset type. Every one of the five lookups may fail and each one abandons the
 *  command list: drawing with no group set, or with the previous item's, is worse than not
 *  drawing. */
EPIX_EXPORT template <std::size_t I>
struct SetMaterialBindGroup {
    template <render::phase::PhaseItem P>
    struct Command {
        void prepare(const epix::ecs::World&) {}

        std::expected<void, render::phase::RenderCommandError> render(
            const P& item,
            std::tuple<>,
            std::optional<epix::ecs::Item<>> entity_item,
            std::tuple<epix::ecs::Res<render::erased_render_asset::ErasedRenderAssets<PreparedMaterial>>,
                       epix::ecs::Res<RenderMaterialInstances>,
                       epix::ecs::Res<MaterialBindGroupAllocators>> params,
            const wgpu::RenderPassEncoder& encoder) {
            (void)entity_item;
            auto&& [materials, instances, allocators] = params;

            const auto instance = instances->instances.find(item.main_entity());
            if (instance == instances->instances.end()) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type = render::phase::RenderCommandError::Type::Skip, .message = {}});
            }
            const MaterialBindGroupAllocator* allocator = allocators->get(instance->second.asset_id.type);
            if (allocator == nullptr) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type = render::phase::RenderCommandError::Type::Skip, .message = {}});
            }
            const PreparedMaterial* material = materials->get(instance->second.asset_id);
            if (material == nullptr) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type = render::phase::RenderCommandError::Type::Skip, .message = {}});
            }
            const auto slab = allocator->get(material->binding.group);
            if (!slab.has_value()) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type = render::phase::RenderCommandError::Type::Skip, .message = {}});
            }
            const wgpu::BindGroup* bind_group = slab->bind_group();
            if (bind_group == nullptr) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type = render::phase::RenderCommandError::Type::Skip, .message = {}});
            }

            encoder.setBindGroup(I, *bind_group, std::span<const std::uint32_t>{});
            return {};
        }
    };
};

}  // namespace epix::pbr

namespace epix::render::phase {

/** @brief The material bind-group command's trait specialisation: one command type, every
 *  phase (Bevy `impl<P: PhaseItem, const I: usize> RenderCommand<P> for
 *  SetMaterialBindGroup<I>`). The same five `Skip` arms as the inner command. */
EPIX_EXPORT template <std::size_t I, PhaseItem P>
struct RenderCommand<::epix::pbr::SetMaterialBindGroup<I>, P> {
    using Param = std::tuple<epix::ecs::Res<::epix::render::erased_render_asset::ErasedRenderAssets<
                                   ::epix::pbr::PreparedMaterial>>,
                             epix::ecs::Res<::epix::pbr::RenderMaterialInstances>,
                             epix::ecs::Res<::epix::pbr::MaterialBindGroupAllocators>>;
    using ViewQuery = std::tuple<>;
    using ItemQuery = epix::ecs::Item<>;

    void prepare(const epix::ecs::World&) {}

    std::expected<void, RenderCommandError> render(const P& item,
                                                   std::tuple<>,
                                                   std::optional<epix::ecs::Item<>> entity_item,
                                                   Param params,
                                                   const wgpu::RenderPassEncoder& encoder) {
        (void)entity_item;
        auto&& [materials, instances, allocators] = params;

        const auto instance = instances->instances.find(item.main_entity());
        if (instance == instances->instances.end()) {
            return std::unexpected(RenderCommandError{.type = RenderCommandError::Type::Skip, .message = {}});
        }
        const ::epix::pbr::MaterialBindGroupAllocator* allocator =
            allocators->get(instance->second.asset_id.type);
        if (allocator == nullptr) {
            return std::unexpected(RenderCommandError{.type = RenderCommandError::Type::Skip, .message = {}});
        }
        const ::epix::pbr::PreparedMaterial* material = materials->get(instance->second.asset_id);
        if (material == nullptr) {
            return std::unexpected(RenderCommandError{.type = RenderCommandError::Type::Skip, .message = {}});
        }
        const auto slab = allocator->get(material->binding.group);
        if (!slab.has_value()) {
            return std::unexpected(RenderCommandError{.type = RenderCommandError::Type::Skip, .message = {}});
        }
        const wgpu::BindGroup* bind_group = slab->bind_group();
        if (bind_group == nullptr) {
            return std::unexpected(RenderCommandError{.type = RenderCommandError::Type::Skip, .message = {}});
        }

        encoder.setBindGroup(I, *bind_group, std::span<const std::uint32_t>{});
        return {};
    }
};

}  // namespace epix::render::phase



template <>
struct std::hash<epix::pbr::ErasedMaterialKey> {
    std::size_t operator()(const epix::pbr::ErasedMaterialKey& key) const noexcept {
        // Bevy hashes the data's TypeId followed by its cached value hash.
        std::size_t h = std::hash<epix::meta::type_index>{}(key.type_id());
        return h ^ (key.hash() + 0x9e3779b9 + (h << 6) + (h >> 2));
    }
};

template <>
struct std::hash<epix::pbr::ErasedMaterialPipelineKey> {
    std::size_t operator()(const epix::pbr::ErasedMaterialPipelineKey& key) const noexcept {
        std::size_t h = static_cast<std::size_t>(key.mesh_key.bits());
        h ^= std::hash<epix::pbr::ErasedMaterialKey>{}(key.material_key) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<epix::meta::type_index>{}(key.type_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
