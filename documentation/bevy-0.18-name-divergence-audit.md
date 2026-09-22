# Bevy 0.18 name-divergence audit

**Status:** findings only. No code may change until the affected rows are approved.

## Method and its limits

Every function *declared* in Epix's headers of the audited modules and the parent modules touched (`mesh`, `image`, `sprite`, `core_graph`, `camera`, `transform`, `pbr`) was extracted, then checked against the pinned source at `tmp/bevy-0.18.0`. A name is listed here when it appears **nowhere** in Bevy's crates.

Two limits, both of which produced wrong interim numbers before being caught:

- The check is **name presence, not semantics**. A row marked *rename* was confirmed by reading Bevy; rows marked *unverified* have not been.
- Three earlier passes used `Select-String -First 1`, a `-First 14` truncation, and a thousand-path `Select-String`, each of which silently under-searched and inflated the "not in Bevy" figure. The lists below were re-derived with the `grep` tool. Early numbers (115/193, then 99/176 for pbr) are therefore provisional; 99 is the corrected count.

Categories:

- **A — rename.** Bevy has the same logic under a different name. Fix: rename to Bevy's.
- **B — extraction.** Bevy writes this inline inside a larger function. Fix: fold into that container.
- **C — keep.** Epix-only by necessity: a Rust method or public field becoming a C++ accessor, an enum-variant accessor where Rust matches inline, the plugin `attach` convention, or parsing noise.

## pbr (99 names)

### Category A — renames, counterpart confirmed by reading Bevy

| ID | Epix name | Bevy counterpart | Location |
| --- | --- | --- | --- |
| ND-1 | `light_probe_frustum_cull` | `LightProbeInfo::frustum_cull` | `light_probe/mod.rs:563` |
| ND-2 | `light_probe_camera_distance_sort_key` | `LightProbeInfo::camera_distance_sort_key` | `light_probe/mod.rs:577` |
| ND-3 | `cluster_center_coordinates` | `ndc_position_to_cluster` | `bevy_light/src/cluster/assign.rs:969` |
| ND-4 | `may_resize_dynamically` | **Category C, not a rename.** Bevy's `ClusterConfig::dynamic_resizing()` is a method sharing its name with the `dynamic_resizing` field, which Rust allows and C++ does not. The build fails with C2365 if renamed, so this name is forced. |
| ND-5 | `view_far_z` | **Category B, not a rename.** `ClusterConfig::first_slice_depth()` already exists at `light_cluster.hpp:159` and is already Bevy's shape (returns 0 for `None`/`Single`, else `z_config.first_slice_depth`). `view_far_z` computes something different — the evaluated far-Z *distance* of the first slice from the mode, the objects' bounds, the view matrix and a scale — which is Bevy's inline slice-to-z work, not its config accessor. Renaming it would have produced two functions named `first_slice_depth` with different meanings; it belongs folded into the cluster assignment instead. |
| ND-45 | **Method note for the whole category A list** | Name-absence alone is not enough to propose a rename. `view_far_z` was classified A purely because the name is absent from Bevy, without checking whether the file *already* had a Bevy-shaped counterpart for that concept — it did. Every remaining A row must be checked against the same file for an existing counterpart before it is renamed. |
| ND-46 | **Tool limitation: members are not distinguished from free functions** | The extraction matches a *declaration shape* and so also captures member functions. `is_cpu_building` (`mesh_instance.hpp:164`) is a member of its struct, not a free function, so it is a false positive. Every row must be checked for whether the thing is a member or free, and against what Bevy has, before its category means anything. |
| ND-47 | **Why Epix has free functions where Bevy has methods** | Three causes, and only the third is a choice rather than a consequence. (1) *Extraction*: Bevy's `assign_objects_to_clusters` (124-860), `prepare_lights` (705-1672) and `MeshPipeline::specialize` (2375-2740) are enormous, so code pulled out of them has no type to belong to and becomes a free function. (2) *Missing owner*: for the probes, `LightProbeInfo` and `RenderViewLightProbes` are not ported, so `frustum_cull` and `camera_distance_sort_key` had nowhere to live and became prefixed free functions. (3) *Member vs free*: where the type does exist, writing a member function was always possible in C++, so a free function there is a choice, not a constraint — each such row should become a member of its Bevy owner. |
| ND-6 | `far_z_at_least_first_slice_depth` | derived from `first_slice_depth`'s clamp | `bevy_light/src/cluster/mod.rs:282` |
| ND-7 | `limit_clusterable_objects_for_uniform_buffer` | **no counterpart** — Bevy has neither this name nor `limit_clusterable_objects_to_uniform_capacity`; the cap is inline in `assign_objects_to_clusters`. Reclassified to **B** (see ND-28). |
| ND-8 | `orthographic_first_slice_depth` | `first_slice_depth`'s orthographic arm | `bevy_light/src/cluster/mod.rs:282` |

### Category A — renames, counterpart **unverified**

**Member/free check now done** (by reading each definition's scope): `refine_sphere_and_scan_x`, `cached_cluster_sphere`, `view_space_object_sphere`, `estimate_cluster_index_count`, `object_cluster_range` and `transform_scale_length` are **free functions** in `light_cluster.hpp`, while Bevy has the equivalent work as methods on `ClusterConfig`/`ClusterZConfig` — so by ND-47's third cause these are the cases where a member was possible and a free function was written instead. Their fix is member-isation, not renaming. `from_binding_type` is a static member of `GpuClusterableObjects`, and `indices_count`/`offsets_count` are members of `ViewClusterBindings`, so those three are not free functions at all.

**Counterpart check now done, by reading `assign.rs`** (not inferred):

| ID | Epix name | Verdict | Evidence |
| --- | --- | --- | --- |
| ND-9 | `refine_sphere_and_scan_x` | **A, but split** | It merges three Bevy functions: `project_to_plane_z` (`assign.rs:655`, defined 1141), `project_to_plane_y` (`:675`, defined 1163) and `get_distance_x` (`:686`, `:701`, defined 1128). The fix is to split it into those names, not to rename it wholesale. |
| ND-10 | `cached_cluster_sphere` | **B** | Bevy holds this as `cluster_aabb_spheres: Local<Vec<Option<Sphere>>>` (`:159`), cleared and refilled per view (`:476-477`) and written in the per-cluster loop (`:724`). It is inline state, not a function. |
| ND-11 | `view_space_object_sphere` | **A — rename** | `cluster_space_clusterable_object_aabb` (`:995`, called at `:391`, `:563`). |
| ND-12 | `estimate_cluster_index_count` | **B** | No Bevy function; the counting is inline in the assignment's second pass. |
| ND-13 | `object_cluster_range` | **A — rename** | `compute_aabb_for_cluster` (`:880`, called at `:729`). |
| ND-14 | `transform_scale_length` | **B** | No Bevy function; part of the aabb and scale work inside the assignment. |

Still unverified rows are listed with their suspicion intact below; the six above are settled.

| ID | Epix name | Suspected Bevy counterpart |
| --- | --- | --- |
| ND-15 | `size_for` | **no counterpart** — `ClusterableObjectType` exists but has no `size_for`, so this is an extraction, not a rename. Reclassified to **B**. |
| ND-16 | `is_cpu_building` | **false positive** — it is a *member* of its struct (`mesh_instance.hpp:164`), not a free function (see ND-46) |
| ND-17 | `from_binding_type` | static member of `GpuClusterableObjects` — counterpart unverified |
| ND-18 | `indices_count` / `offsets_count` | **Category C — field accessors.** Bevy's `ViewClusterBindings` has private fields `n_indices` and `n_offsets` (`bevy_pbr/src/cluster.rs:148-149`). Rust reads them in-crate; C++ needs accessors, so the divergence is forced. Naming them `n_indices()`/`n_offsets()` would mirror Bevy's fields more closely if preferred. |

### Category B — extractions

| ID | Epix name(s) | Bevy container it belongs in |
| --- | --- | --- |
| ND-19 | `point_light_shadow_view_requests`, `point_light_face_world_from_view`, `point_light_face_clip_from_world`, `point_light_shadow_array_layer`, `point_light_shadow_pass_name`, `cube_face_view_rotations`, `point_light_shadow_view_count` | `prepare_lights` (`render/light.rs:705-1672`) |
| ND-20 | `directional_shadow_view_requests`, `directional_shadow_pass_name`, `directional_cascade_frusta`, `directional_view_frustum`, `to_cascades_frusta`, `fill_gpu_directional_cascades`, `directional_shadow_slices` | `prepare_lights` |
| ND-21 | `should_update_point_light_frusta`, `should_update_directional_cascade_frusta` | `prepare_lights` / the frusta systems |
| ND-22 | `point_light_custom_data`, `point_light_gpu_flags`, `spot_light_custom_data`, `directional_light_gpu_flags`, `directional_light_sort_key`, `point_light_sort_key`, `count_lights`, `resolve_ambient_light`, `pack_ambient_color`, `pack_directional_color`, `pack_light_color_intensity_range`, `pack_cluster_dimensions`, `pack_cluster_factors`, `default_gpu_lights`, `directional_light_shadow_texture_layers`, `directional_light_shadow_texture_size`, `point_light_shadow_texture_layers`, `spot_light_shadowmap_offset`, `spot_light_y_negative`, `num_directional_cascades_enabled` | `extract_lights` / `prepare_lights` (`light_meta.hpp` mirrors `LightMeta` + the packing inside `prepare_lights`) |
| ND-23 | `specialize_mesh_vertex_layout`, `specialize_mesh_pass`, `specialize_mesh_key_shader_defs`, `specialize_mesh_capability_shader_defs`, `mesh_pipeline_primitive_state`, `mesh_pipeline_depth_state`, `mesh_pipeline_multisample_state` | `MeshPipeline::specialize` (`render/mesh.rs:2375-2740`) |
| ND-24 | `gpu_fog_from`, `fog_exponential_density`, `fog_exponential_squared_density`, `fog_atmospheric_extinction`, `fog_atmospheric_inscattering` | `FogFalloff`'s constructors and `prepare_fog` (`fog.rs:318-462`, `render/fog.rs:64-113`) |
| ND-25 | `environment_map_uniform_from`, `environment_map_uniform_default` | `gather_environment_map_uniform` (`light_probe/mod.rs:324-343`) |
| ND-26 | `light_probes_gather_list` | `RenderViewLightProbes::maybe_gather_light_probes` (`light_probe/mod.rs:636`) |
| ND-27 | `build_cascade_shadow_config`, `default_cascade_shadow_config`, `build_view_cascades`, `cascade_bounds` | `DirectionalLightShadowMap`/cascade setup — container unverified |
| ND-28 | `pack_cluster_offsets_and_counts_storage`, `pack_index_into_uniform_word`, `uniform_index_lists_array_index`, `uniform_index_lists_component`, `uniform_index_lists_sub_index`, `uniform_offsets_array_index`, `uniform_offsets_component`, `insert_object_into_cluster`, `insert_object_into_cluster_row`, `prepare_view_cluster_bindings`, `extract_view_clusters` | `bevy_light/src/cluster/assign.rs` + the cluster bindings — extractions over Bevy's structure |
| ND-29 | `validated_shadow_map_size` | **rename**, not an extraction: Bevy's `validate_shadow_map_size` is a system at `bevy_light/src/directional_light.rs:198`. Reclassified to **A**. |

### Category C — keep

`bits_`, `constexpr`, `operator` (parsing noise); `from_msaa`, `from_pipeline_key`, `from_prepass_textures` (Bevy trait impls, false positives); `assign_object_to_view_clusters`, `point_light_assignment_data`, `spot_light_assignment_data`, `light_probe_assignment_data`, `decal_assignment_data`, `cluster_assignment_bounds`, `cluster_bounding_sphere`, `cluster_z_planes` (need individual review against `assign.rs` before classifying — listed here pending).

## camera (20 names)

Category A / C, all small: `as_orthographic`, `as_perspective`, `get_custom`, `is_inverted`, `get_far`, `far_value`, `update_projection`, `from_normal_d`, `layer_bit`, `set_view_visible`, `all_except` (variant/bit accessors and helpers Rust writes inline — **category C**, keep). `check_visibility_system` — **confirmed A**: Bevy's `check_visibility` is at `bevy_camera/src/visibility/mod.rs:588`, so this is a rename of an instantiated system. Sub-view helpers, checked against Bevy's `SubCameraView` (`bevy_camera/src/camera.rs:141`, fields `full_size`, `offset`, `size`): `full_size` is already Bevy's field name and is an accessor for it — **C**; `sub_size` maps to Bevy's `size` — **A**; `crop_to_sub_view` and `clip_from_view_for_sub` have no counterpart name in Bevy, so they are **B** unless `SubCameraView`'s impl turns out to carry them. `from_view_projection`, `from_primary`, `from_window`, `from_texture`, `from_manual_texture_view`, `RenderTargetId` — **checked, absent from Bevy by name**, so these are Epix-shaped factories and helpers rather than renames; `exposure_from_physical_camera` likewise absent, classification pending.

## mesh (28 names)

Verified against `bevy_mesh/src` this round.

**Category A — renames, counterpart confirmed:**

| ID | Epix name | Bevy counterpart |
| --- | --- | --- |
| ND-35 | `add_attribute` | `Mesh::insert_attribute` (`mesh.rs:370`) |
| ND-36 | `get_attribute_mut` | `Mesh::attribute_mut` (`mesh.rs:587`) |
| ND-37 | `set_attributes` | `Mesh::attributes_mut` (`mesh.rs:652`) |
| ND-38 | `set_primitive_type`, `with_primitive_type` | `Mesh::primitive_topology` / `BaseMeshPipelineKey::from_primitive_topology` (`mesh.rs:356`, `lib.rs:72`) |
| ND-39 | `try_build` | `MeshBuilder::build` (`primitives/mod.rs:45` and every builder) |
| ND-40 | `mesh_triangles_view` | `Mesh::triangles` (`mesh.rs:2054`) |
| ND-41 | `lowest_morph_texture_2d` | `Mesh::morph_targets` family (`mesh.rs:2226-2328`) |
| ND-42 | `set_array_stride`, `set_shader_location`, `set_step_mode` | `VertexBufferLayout` — Bevy builds these via `from_vertex_formats` and fields (`vertex.rs:414`) |
| ND-43 | `vertex_format_size` | `VertexFormat::size` — unverified but reads as a rename |
| ND-44 | `perimeter_range`, `perimeter_step_range` | `Extrudable`'s perimeter internals (`primitives/extrusion.rs:67,137`) |

**Category B — extractions:** `make_circle`, `make_box2d`, `make_box2d_uv`, `from_polygon`, `from_inner_and_outer`, `try_convert_vertex_attribute_values`, `vertex_arrays_to_vectors`, `extracted_to_render_world`. Each is builder or conversion glue Bevy writes inline; the container for each needs its own lookup before folding. `perimeter_range` and `perimeter_step_range` were here and have moved to category A above.

**Category C — keep:** every `with_*` builder setter (`with_resolution`, `with_rings`, `with_latitudes`, `with_longitudes`, `with_segments`, `with_major_resolution`, `with_minor_resolution`, `with_angle_range`, `with_normal`, `with_uv_mode`, `with_uv_profile`, `with_kind`) — Bevy exposes those as **public fields** on the builder structs, so C++ needs accessors. Also `as_u16`, `sizeof`, `get_if`, `bits_`, `serialize_serialized_mesh`, `deserialize_serialized_mesh` (Epix utilities and serde-shaped code).


## core_graph (7 names)

- **ND-30** `split_transmissive_range` — **B.** Bevy splits the transmissive items *inline* in `main_transmissive_pass_3d_node` (`bevy_core_pipeline/src/core_3d/main_transmissive_pass_3d_node.rs:81-110`, "split them into N = `screen_space_specular_transmission_steps`"). No separate function exists.
- **ND-31** `max_mips_for_size` — **A, rename.** Bevy calls this `ImageSize::max_mips(TextureDimension::D2)` (`experimental/mip_generation/mod.rs:597`). `depth_pyramid_size` is Bevy's `ImageSize` type; `depth_pyramid_mip_count` is the local it assigns to.
- **ND-32** `prepass_pipeline_targets` — **B.** No counterpart name found in Bevy; the target list is assembled inline where the prepass pipelines are prepared.
- **ND-33** `prepare_skybox_uniforms` — **B, split.** Bevy has `prepare_skybox_pipelines` and `prepare_skybox_bind_groups` (`skybox/mod.rs:243,270`, plus the prepass variants at `skybox/prepass.rs:114,138`). This name is absent from Bevy; it groups work Bevy separates.
- **ND-34** `fullscreen_material_sub_graph` — container still unverified.
- Category C: `add_to`, `epix_core2d_bin_key_hash`.

## mesh rows completed

- **ND-44** `perimeter_range`, `perimeter_step_range` — **A, rename.** Bevy's `Extrudable` trait declares `fn perimeter(&self) -> Vec<PerimeterSegment>` (`bevy_mesh/src/primitives/extrusion.rs:85`, implemented per shape throughout `dim2.rs`).
- **ND-43** `vertex_format_size` — **B/C, not a rename.** Checked: Bevy's `VertexFormat` has no `size()`; the `fn size()` implementations in the tree belong to `Window`, `URect`, `IRect`, `ImageSize` and `BatchedUniformBuffer`. This is an Epix helper.

## Still unverified

`size_for` and `from_binding_type` — both absent from Bevy by name. `from_binding_type` is a static member of `GpuClusterableObjects`, and Bevy's counterpart there is `GpuClusterableObjects::new(buffer_binding_type)` (`bevy_pbr/src/cluster.rs:172`), which makes it a likely **A**. `size_for` has no counterpart on `ClusterableObjectType`, so **B/C**.

## image (47 names)

**All category C.** They are ports of the `rectangle-pack` and `guillotiere` crates (`add_new_sections`, `all_empty_space_*`, `box_size_heuristic`, `try_place`, `sort_bins_*`, `find_suitable_rect`, `free_list_for_size`, `available_bin_sections`, `width_largest_*`, …), which Bevy uses as *dependencies* rather than vendoring, so the names live in crate sources and not in `bevy_*.rs`. Not mismatches. The remainder (`has_data`, `take_data`, `is_float_texture_format`, `srgb_texture_format`, `texture_format_components`, `get_or_init_sampler_descriptor`, `resize_size`, `adjust_size`, `same_extent`, `SUPPORTED_FORMATS`, `to_debug_string`, `is_decodable`, `AtlasAllocator`, `alloc_id`, `guillotine_rect`, `new_spread`, `volume_heuristic`, `push_available_bin_section*`, `remove_filled_section`, `depth_*` heuristics) are the same family.

## sprite, transform (0 names)

Every declared function name in both modules exists in Bevy. No rows.
