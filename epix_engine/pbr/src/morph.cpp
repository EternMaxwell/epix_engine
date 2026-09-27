#include <epix/pbr/morph.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace epix::pbr {

void add_to_alignment_f32(render::render_resource::RawBufferVec<float>& buffer) {
    using render::render_resource::RawBufferVec;
    constexpr std::size_t WGPU_MIN_ALIGN = 256;
    constexpr std::size_t element_size   = sizeof(float);
    static_assert(WGPU_MIN_ALIGN % element_size == 0 || element_size % WGPU_MIN_ALIGN == 0,
                  "a RawBufferVec element must divide or be divided by the 256-byte uniform alignment");

    const std::size_t byte_size   = buffer.len() * element_size;
    const std::size_t bytes_over  = byte_size % WGPU_MIN_ALIGN;
    if (bytes_over == 0) return;
    const std::size_t to_add = (WGPU_MIN_ALIGN - bytes_over) / element_size;
    // Bevy extends with `T::default()`, which for `f32` is zero.
    for (std::size_t index = 0; index < to_add; ++index) buffer.push(0.0f);
}

void prepare_morphs(ecs::Res<wgpu::Device> render_device, ecs::Res<wgpu::Queue> render_queue,
                    ecs::ResMut<MorphUniforms> uniform) {
    MorphUniforms& uniforms = *uniform;
    if (uniforms.current_buffer.is_empty()) return;

    const std::size_t len = uniforms.current_buffer.len();
    uniforms.current_buffer.reserve(len, *render_device);
    uniforms.current_buffer.write_buffer(*render_device, *render_queue);

    // `prev_buffer` is deliberately not written: it already holds last frame's
    // weights and that data is still on the GPU.
}

void extract_morphs(
    ecs::ResMut<MorphIndices> morph_indices, ecs::ResMut<MorphUniforms> uniform,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const camera::ViewVisibility&, const mesh::MorphWeights&>>> query) {
    MorphIndices& indices = *morph_indices;
    MorphUniforms& uniforms = *uniform;

    // Swap, then clear: what was current becomes the previous frame's record,
    // which is what the motion-vector path reads.
    std::swap(indices.current, indices.prev);
    std::swap(uniforms.current_buffer, uniforms.prev_buffer);
    indices.current.clear();
    uniforms.current_buffer.clear();

    for (auto&& [entity, view_visibility, morph_weights] : query.iter()) {
        if (!view_visibility.get()) continue;

        const std::size_t start = uniforms.current_buffer.len();
        const std::span<const float> weights = morph_weights.weights();
        // A fixed-size uniform binding cannot be sized per mesh, so the weights
        // are padded to `MAX_MORPH_WEIGHTS` with zeros and the shader reads only
        // the count it was given.
        for (std::size_t index = 0; index < mesh::MAX_MORPH_WEIGHTS; ++index) {
            uniforms.current_buffer.push(index < weights.size() ? weights[index] : 0.0f);
        }
        add_to_alignment_f32(uniforms.current_buffer);

        // The recorded offset is in bytes, as the binding offset is.
        indices.current.emplace(render::sync_world::MainEntity{entity},
                                MorphIndex{.index = static_cast<std::uint32_t>(start * sizeof(float))});
    }
}

void no_automatic_morph_batching(
    ecs::Commands commands,
    ecs::Query<ecs::Item<ecs::Entity>,
               ecs::Filter<ecs::With<mesh::MorphWeights>, ecs::Without<render::phase::NoAutomaticBatching>>> query) {
    for (auto&& [entity] : query.iter()) {
        commands.entity(entity).insert(render::phase::NoAutomaticBatching{});
    }
}

}  // namespace epix::pbr
