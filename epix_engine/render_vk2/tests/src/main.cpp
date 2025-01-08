#include <epix/app.h>
#include <epix/input.h>
#include <epix/rdvk.h>
#include <epix/window.h>

#include <filesystem>
#include <spirv_glsl.hpp>

#include "shaders/vertex_shader.h"

using namespace epix::render::vulkan2::backend;

std::vector<DescriptorSetLayout> create_descriptor_set_layout(
    Device& device,
    spirv_cross::CompilerGLSL& glsl,
    vk::ShaderStageFlags stage,
    uint32_t max_descriptor_count = 65536
) {
    std::vector<std::vector<vk::DescriptorSetLayoutBinding>> bindings;
    auto&& resources = glsl.get_shader_resources();
    for (auto& resource : resources.uniform_buffers) {
        auto& type = glsl.get_type(resource.type_id);
        auto set =
            glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        if (set >= bindings.size()) {
            bindings.resize(set + 1);
        }
        auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        bindings[set].push_back(
            vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                .setDescriptorCount(
                    type.array.size() ? (type.array_size_literal[0]
                                             ? type.array_size_literal[0]
                                             : max_descriptor_count)
                                      : 1
                )
                .setStageFlags(stage)
        );
    }
    for (auto& resource : resources.storage_buffers) {
        auto& type = glsl.get_type(resource.type_id);
        auto set =
            glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        if (set >= bindings.size()) {
            bindings.resize(set + 1);
        }
        auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        bindings[set].push_back(
            vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                .setDescriptorCount(
                    type.array.size() ? (type.array_size_literal[0]
                                             ? type.array_size_literal[0]
                                             : max_descriptor_count)
                                      : 1
                )
                .setStageFlags(stage)
        );
    }
    for (auto& resource : resources.sampled_images) {
        auto& type = glsl.get_type(resource.type_id);
        auto set =
            glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        if (set >= bindings.size()) {
            bindings.resize(set + 1);
        }
        auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        bindings[set].push_back(
            vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                .setDescriptorCount(
                    type.array.size() ? (type.array_size_literal[0]
                                             ? type.array_size_literal[0]
                                             : max_descriptor_count)
                                      : 1
                )
                .setStageFlags(stage)
        );
    }
    for (auto& resource : resources.separate_images) {
        auto& type = glsl.get_type(resource.type_id);
        auto set =
            glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        if (set >= bindings.size()) {
            bindings.resize(set + 1);
        }
        auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        bindings[set].push_back(
            vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(
                    type.image.dim == spv::Dim::DimBuffer
                        ? vk::DescriptorType::eStorageTexelBuffer
                        : vk::DescriptorType::eSampledImage
                )
                .setDescriptorCount(
                    type.array.size() ? (type.array_size_literal[0]
                                             ? type.array_size_literal[0]
                                             : max_descriptor_count)
                                      : 1
                )
                .setStageFlags(stage)
        );
    }
    for (auto& resource : resources.separate_samplers) {
        auto& type = glsl.get_type(resource.type_id);
        auto set =
            glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        if (set >= bindings.size()) {
            bindings.resize(set + 1);
        }
        auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        bindings[set].push_back(
            vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(vk::DescriptorType::eSampler)
                .setDescriptorCount(
                    type.array.size() ? (type.array_size_literal[0]
                                             ? type.array_size_literal[0]
                                             : max_descriptor_count)
                                      : 1
                )
                .setStageFlags(stage)
        );
    }
    for (auto& resource : resources.storage_images) {
        auto& type = glsl.get_type(resource.type_id);
        auto set =
            glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        if (set >= bindings.size()) {
            bindings.resize(set + 1);
        }
        auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        bindings[set].push_back(
            vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(
                    type.image.dim == spv::Dim::DimBuffer
                        ? vk::DescriptorType::eStorageTexelBuffer
                        : vk::DescriptorType::eStorageImage
                )
                .setDescriptorCount(
                    type.array.size() ? (type.array_size_literal[0]
                                             ? type.array_size_literal[0]
                                             : max_descriptor_count)
                                      : 1
                )
                .setStageFlags(stage)
        );
    }
    for (auto& resource : resources.subpass_inputs) {
        auto& type = glsl.get_type(resource.type_id);
        auto set =
            glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        if (set >= bindings.size()) {
            bindings.resize(set + 1);
        }
        auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        bindings[set].push_back(
            vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(vk::DescriptorType::eInputAttachment)
                .setDescriptorCount(
                    type.array.size() ? (type.array_size_literal[0]
                                             ? type.array_size_literal[0]
                                             : max_descriptor_count)
                                      : 1
                )
                .setStageFlags(stage)
        );
    }
    std::vector<DescriptorSetLayout> layouts;
    for (auto& binding : bindings) {
        layouts.push_back(device.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo().setBindings(binding)
        ));
    }
    return std::move(layouts);
}

int main() {
    using namespace epix::app;
    using namespace epix::window;

    App app2 = App::create2();
    app2.add_plugin(WindowPlugin{});
    app2.get_plugin<WindowPlugin>()->primary_desc().set_vsync(false);
    app2.add_plugin(
        epix::render::vulkan2::RenderVKPlugin{}.set_debug_callback(true)
    );
    app2.add_plugin(epix::input::InputPlugin{});
    app2.run();

    std::vector<uint32_t> spirv(
        vertex_spv, vertex_spv + sizeof(vertex_spv) / sizeof(uint32_t)
    );
    spirv_cross::CompilerGLSL compiler(spirv);

    auto resources = compiler.get_shader_resources();

    for (auto& resource : resources.push_constant_buffers) {
        auto& buffer_type = compiler.get_type(resource.type_id);
        auto& buffer_name = compiler.get_name(resource.id);
        std::cout << "Push constant buffer: " << buffer_name << std::endl;
        for (auto& member : buffer_type.member_types) {
            auto& member_type = compiler.get_type(member);
            auto& member_name =
                compiler.get_member_name(resource.base_type_id, member);
            std::cout << "\tmember name: " << member_name << std::endl;
        }
    }
    for (auto& resource : resources.uniform_buffers) {
        auto& buffer_type = compiler.get_type(resource.type_id);
        auto& buffer_name = compiler.get_name(resource.id);
        auto set =
            compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        auto binding =
            compiler.get_decoration(resource.id, spv::DecorationBinding);
        auto array_size = buffer_type.array[0];
        std::cout << std::format(
                         "set = {}, binding = {}, Uniform buffer: {}, array "
                         "size literal = {}",
                         set, binding, buffer_name, array_size
                     )
                  << std::endl;
        for (size_t i = 0; i < buffer_type.member_types.size(); i++) {
            auto& member_type = compiler.get_type(buffer_type.member_types[i]);
            auto& member_name =
                compiler.get_member_name(resource.base_type_id, i);
            std::cout << "\tmember name: " << member_name << std::endl;
        }
    }
    for (auto& resource : resources.storage_buffers) {
        auto& buffer_type = compiler.get_type(resource.type_id);
        auto& buffer_name = compiler.get_name(resource.id);
        auto set =
            compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        auto binding =
            compiler.get_decoration(resource.id, spv::DecorationBinding);
        auto array_size = buffer_type.array[0];
        std::cout << std::format(
                         "set = {}, binding = {}, Storage buffer: {}, array "
                         "size literal = {}",
                         set, binding, buffer_name, array_size
                     )
                  << std::endl;
        for (size_t i = 0; i < buffer_type.member_types.size(); i++) {
            auto& member_type = compiler.get_type(buffer_type.member_types[i]);
            auto& member_name =
                compiler.get_member_name(resource.base_type_id, i);
            std::cout << "\tmember name: " << member_name << std::endl;
        }
    }
    for (auto& resource : resources.sampled_images) {
        auto& image_type = compiler.get_type(resource.type_id);
        auto& image_name = compiler.get_name(resource.id);
        auto set =
            compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        auto binding =
            compiler.get_decoration(resource.id, spv::DecorationBinding);
        auto array_size = image_type.array[0];
        std::cout
            << std::format(
                   "set = {}, binding = {}, Sampled image: {}, array size = {}",
                   set, binding, image_name, array_size
               )
            << std::endl;
    }
}