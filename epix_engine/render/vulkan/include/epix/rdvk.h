#pragma once

#include <epix/app.h>
#include <epix/common.h>
#include <epix/window.h>

#include <stack>
#include <variant>

#include "epix/rdvk2/rdvk_basic.h"
#include "epix/rdvk2/rdvk_utils.h"

namespace epix::render::vulkan2 {
struct RenderContext;
struct VulkanPlugin;
struct CtxCmdBuffer;
struct VulkanResources;
namespace systems {
using namespace epix::render::vulkan2::backend;
using epix::Command;
using epix::Extract;
using epix::Get;
using epix::Query;
using epix::Res;
using epix::ResMut;
using epix::With;
using epix::Without;
using window::components::PrimaryWindow;
using window::components::Window;
EPIX_API void create_context(
    Command cmd,
    Query<Get<Window>, With<PrimaryWindow>> query,
    Res<VulkanPlugin> plugin
);
EPIX_API void destroy_context(
    Command cmd, ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd
);
}  // namespace systems
struct RenderContext {
   public:
    backend::Instance instance;
    mutable backend::PhysicalDevice physical_device;
    mutable backend::Device device;
    backend::Queue queue;
    backend::CommandPool command_pool;
    mutable backend::Surface primary_surface;
    mutable backend::Swapchain primary_swapchain;

    friend EPIX_API void systems::create_context(
        Command cmd,
        Query<
            Get<window::components::Window>,
            With<window::components::PrimaryWindow>> query,
        Res<VulkanPlugin> plugin
    );
    friend EPIX_API void systems::destroy_context(
        Command cmd, ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd
    );
};
struct CtxCmdBuffer {
   public:
    backend::CommandBuffer cmd_buffer;
    backend::Fence fence;

    friend EPIX_API void systems::create_context(
        Command cmd,
        Query<
            Get<window::components::Window>,
            With<window::components::PrimaryWindow>> query,
        Res<VulkanPlugin> plugin
    );
    friend EPIX_API void systems::destroy_context(
        Command cmd, ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd
    );
};
namespace systems {
using namespace epix::render::vulkan2::backend;
using epix::Command;
using epix::Extract;
using epix::Get;
using epix::Query;
using epix::Res;
using epix::ResMut;
using epix::With;
using epix::Without;
using window::components::PrimaryWindow;
using window::components::Window;
EPIX_API void extract_context(
    ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd, Command cmd
);
EPIX_API void clear_extracted_context(
    ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd, Command cmd
);
EPIX_API void recreate_swap_chain(
    ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd
);
EPIX_API void get_next_image(
    ResMut<RenderContext> context,
    ResMut<CtxCmdBuffer> ctx_cmd,
    ResMut<VulkanResources> res_manager
);
EPIX_API void present_frame(
    ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd
);
}  // namespace systems
struct VulkanPlugin : public epix::Plugin {
    bool debug_callback = false;
    EPIX_API VulkanPlugin& set_debug_callback(bool debug);
    EPIX_API void build(epix::App& app) override;
};
}  // namespace epix::render::vulkan2

namespace epix::render::vulkan2 {
struct VulkanResources;
namespace systems {
using namespace epix::render::vulkan2::backend;
using epix::Command;
using epix::Extract;
using epix::Get;
using epix::Query;
using epix::Res;
using epix::ResMut;
using epix::With;
using epix::Without;

EPIX_API void create_res_manager(Command cmd, Res<RenderContext> context);
EPIX_API void destroy_res_manager(
    Command cmd, ResMut<VulkanResources> res_manager
);
EPIX_API void extract_res_manager(
    ResMut<VulkanResources> res_manager, Command cmd
);
EPIX_API void clear_extracted(ResMut<VulkanResources> res_manager, Command cmd);
}  // namespace systems
struct VulkanResources {
    using Device    = backend::Device;
    using Buffer    = backend::Buffer;
    using Image     = backend::Image;
    using ImageView = backend::ImageView;
    using Sampler   = backend::Sampler;

   private:
    mutable Device m_device;

    std::vector<Buffer> buffers;
    std::vector<std::string> buffer_names;
    entt::dense_map<std::string, uint32_t> buffer_map;
    entt::dense_set<uint32_t> buffer_cache_remove;
    std::vector<std::pair<std::string, uint32_t>> buffer_add_cache;
    std::stack<uint32_t> buffer_free_indices;

    std::vector<Image> images;
    std::vector<std::string> image_names;
    entt::dense_map<std::string, uint32_t> image_map;
    entt::dense_set<uint32_t> image_cache_remove;
    std::vector<std::pair<std::string, uint32_t>> image_add_cache;
    std::stack<uint32_t> image_free_indices;

    std::vector<ImageView> image_views;
    std::vector<std::string> image_view_names;
    entt::dense_map<std::string, uint32_t> image_view_map;
    std::vector<std::pair<uint32_t, ImageView>> view_cache;
    entt::dense_set<uint32_t> view_cache_remove;
    std::vector<std::pair<std::string, uint32_t>> view_add_cache;
    std::stack<uint32_t> view_free_indices;

    std::vector<Sampler> samplers;
    std::vector<std::string> sampler_names;
    entt::dense_map<std::string, uint32_t> sampler_map;
    std::vector<std::pair<uint32_t, Sampler>> sampler_cache;
    entt::dense_set<uint32_t> sampler_cache_remove;
    std::vector<std::pair<std::string, uint32_t>> sampler_add_cache;
    std::stack<uint32_t> sampler_free_indices;

    vk::DescriptorPool descriptor_pool;
    vk::DescriptorSetLayout descriptor_set_layout;
    vk::DescriptorSet descriptor_set;

    EPIX_API VulkanResources(Device device);
    EPIX_API void apply_cache();

   public:
    EPIX_API Device& device() const;
    EPIX_API void destroy();
    EPIX_API uint32_t add_buffer(const std::string& name, Buffer buffer);
    EPIX_API uint32_t add_image(const std::string& name, Image image);
    EPIX_API uint32_t
    add_image_view(const std::string& name, ImageView image_view);
    EPIX_API uint32_t add_sampler(const std::string& name, Sampler sampler);
    EPIX_API Buffer replace_buffer(const std::string& name, Buffer buffer);
    EPIX_API Image replace_image(const std::string& name, Image image);
    EPIX_API ImageView
    replace_image_view(const std::string& name, ImageView image_view);
    EPIX_API Sampler replace_sampler(const std::string& name, Sampler sampler);

    EPIX_API bool contains_buffer(const std::string& name) const;
    EPIX_API bool contains_image(const std::string& name) const;
    EPIX_API bool contains_image_view(const std::string& name) const;
    EPIX_API bool contains_sampler(const std::string& name) const;

    EPIX_API Buffer get_buffer(const std::string& name) const;
    EPIX_API Image get_image(const std::string& name) const;
    EPIX_API ImageView get_image_view(const std::string& name) const;
    EPIX_API Sampler get_sampler(const std::string& name) const;
    EPIX_API Buffer get_buffer(uint32_t index) const;
    EPIX_API Image get_image(uint32_t index) const;
    EPIX_API ImageView get_image_view(uint32_t index) const;
    EPIX_API Sampler get_sampler(uint32_t index) const;

    EPIX_API void remove_buffer(const std::string& name);
    EPIX_API void remove_image(const std::string& name);
    EPIX_API void remove_image_view(const std::string& name);
    EPIX_API void remove_sampler(const std::string& name);
    EPIX_API void remove_buffer(uint32_t index);
    EPIX_API void remove_image(uint32_t index);
    EPIX_API void remove_image_view(uint32_t index);
    EPIX_API void remove_sampler(uint32_t index);

    EPIX_API uint32_t buffer_index(const std::string& name) const;
    EPIX_API uint32_t image_index(const std::string& name) const;
    EPIX_API uint32_t image_view_index(const std::string& name) const;
    EPIX_API uint32_t sampler_index(const std::string& name) const;

    EPIX_API vk::DescriptorSet get_descriptor_set() const;
    EPIX_API vk::DescriptorSetLayout get_descriptor_set_layout() const;

    friend EPIX_API void systems::create_res_manager(
        Command cmd, Res<RenderContext> context
    );
    friend EPIX_API void systems::destroy_res_manager(
        Command cmd, ResMut<VulkanResources> res_manager
    );
    friend EPIX_API void systems::extract_res_manager(
        ResMut<VulkanResources> res_manager, Command cmd
    );
};
}  // namespace epix::render::vulkan2

namespace epix::render::vulkan2 {
template <typename VertT, typename... Ts>
struct Mesh {
   private:
    using types = std::tuple<VertT, Ts...>;
    std::tuple<std::vector<VertT>, std::vector<Ts>...> data;
    const std::array<bool, sizeof...(Ts) + 1> input_rate_instance;
    std::variant<void*, std::vector<uint16_t>, std::vector<uint32_t>> indices;

    template <size_t I = 0>
    std::vector<size_t> vertex_counts() const {
        if constexpr (I == sizeof...(Ts)) {
            if (input_rate_instance[I]) {
                return {};
            } else {
                return {std::get<I>(data).size()};
            }
        } else {
            if (input_rate_instance[I]) {
                return vertex_counts<I + 1>();
            } else {
                auto counts = vertex_counts<I + 1>();
                counts.push_back(std::get<I>(data).size());
                return std::move(counts);
            }
        }
    }

    template <size_t I = 0>
    std::vector<size_t> instance_counts() const {
        if constexpr (I == sizeof...(Ts)) {
            if (input_rate_instance[I]) {
                return {std::get<I>(data).size()};
            } else {
                return {};
            }
        } else {
            if (input_rate_instance[I]) {
                auto counts = instance_counts<I + 1>();
                counts.push_back(std::get<I>(data).size());
                return std::move(counts);
            } else {
                return instance_counts<I + 1>();
            }
        }
    }

   public:
    template <typename... Args>
        requires(std::same_as<bool, Args> && ...) &&
                    (sizeof...(Args) == sizeof...(Ts) + 1)
    Mesh(Args... args) : input_rate_instance(args...), indices(nullptr) {}
    Mesh() : input_rate_instance(false), indices(nullptr) {}

    void clear() {
        std::apply([](auto&... vertices) { (vertices.clear(), ...); }, data);
        if (has_indices()) {
            if (indices16bit()) {
                indices16().clear();
            } else {
                indices32().clear();
            }
        }
    }

    template <size_t I = 0, typename... Args>
        requires(std::constructible_from<
                 std::tuple_element_t<I, types>,
                 Args...>)
    void emplace_vertex(Args... args) {
        std::get<I>(data).emplace_back(args...);
    }
    template <size_t I = 0>
    std::vector<std::tuple_element_t<I, types>>& vertices() {
        return std::get<I>(data);
    }
    template <size_t I = 0>
    const std::vector<std::tuple_element_t<I, types>>& vertices() const {
        return std::get<I>(data);
    }

    void emplace_index(size_t index) {
        if (has_indices()) {
            if (indices16bit()) {
                indices16().emplace_back(index);
            } else {
                indices32().emplace_back(index);
            }
        } else {
            spdlog::warn("Index type not set, cannot emplace indices.");
        }
    }
    std::vector<uint16_t>& indices16() {
        return std::get<std::vector<uint16_t>>(indices);
    }
    const std::vector<uint16_t>& indices16() const {
        return std::get<std::vector<uint16_t>>(indices);
    }
    std::vector<uint32_t>& indices32() {
        return std::get<std::vector<uint32_t>>(indices);
    }
    const std::vector<uint32_t>& indices32() const {
        return std::get<std::vector<uint32_t>>(indices);
    }

    template <typename T, size_t I = 0>
        requires requires(T&& t) {
            { std::get<I>(data) = std::forward<T>(t) };
        }
    void set_vertices(T&& vertices) {
        std::get<I>(data) = std::forward<T>(vertices);
    }

    void set_16bit_indices() {
        if (std::holds_alternative<void*>(indices)) {
            indices = std::vector<uint16_t>();
        } else if (indices32bit()) {
            auto& _indices32 = indices32();
            indices =
                std::vector<uint16_t>(_indices32.begin(), _indices32.end());
        }
    }
    void set_32bit_indices() {
        if (std::holds_alternative<void*>(indices)) {
            indices = std::vector<uint32_t>();
        } else if (indices16bit()) {
            auto& _indices16 = indices16();
            indices =
                std::vector<uint32_t>(_indices16.begin(), _indices16.end());
        }
    }
    void no_indices() { indices = nullptr; }
    bool has_indices() const {
        return !std::holds_alternative<void*>(indices) &&
               (indices16bit() || indices32bit());
    }
    bool indices16bit() const {
        return std::holds_alternative<std::vector<uint16_t>>(indices);
    }
    bool indices32bit() const {
        return std::holds_alternative<std::vector<uint32_t>>(indices);
    }

    template <typename T>
        requires requires(T&& t) {
            { indices = std::forward<T>(t) };
        }
    void set_indices(T&& vertices) {
        indices = std::forward<T>(vertices);
    }

    std::optional<size_t> vertex_count() const {
        auto counts = vertex_counts();
        if (counts.size() == 0) {
            return {};
        }
        for (auto count : counts) {
            if (count != counts[0]) {
                return {};
            }
        }
        return counts[0];
    }
    std::optional<size_t> instance_count() const {
        auto counts = instance_counts();
        if (counts.size() == 0) {
            return 1;
        }
        for (auto count : counts) {
            if (count != counts[0]) {
                return {};
            }
        }
        return counts[0];
    }
    std::optional<size_t> index_count() const {
        if (has_indices()) {
            if (indices16bit()) {
                return std::get<std::vector<uint16_t>>(indices).size();
            } else {
                return std::get<std::vector<uint32_t>>(indices).size();
            }
        }
        return {};
    }
};
constexpr double mesh_buffer_growth_factor = 1.2;
constexpr uint32_t min_size                = 16;
template <typename MeshT>
struct StagingMesh {};
template <typename MeshT>
struct GPUMesh {};
template <typename VertT, typename... Ts>
struct StagingMesh<Mesh<VertT, Ts...>> {
    using types = std::tuple<VertT, Ts...>;

   private:
    backend::Device _device;
    std::array<backend::Buffer, sizeof...(Ts) + 1> _vertex_buffers;
    std::array<size_t, sizeof...(Ts) + 1> _buffer_sizes;
    std::array<size_t, sizeof...(Ts) + 1> _buffer_capacities;
    backend::Buffer _index_buffer = {};
    vk::IndexType _index_type     = vk::IndexType::eUint16;
    size_t _index_buffer_size     = 0;
    size_t _index_buffer_capacity = 0;

    std::optional<size_t> _vertex_count;
    std::optional<size_t> _instance_count;
    std::optional<size_t> _index_count;

    template <size_t I = 0>
    void resize_buffer(const Mesh<VertT, Ts...>& mesh) {
        using T      = std::tuple_element_t<I, types>;
        auto& buffer = _vertex_buffers[I];
        if (!buffer ||
            _buffer_capacities[I] < sizeof(T) * mesh.vertices<I>().size()) {
            if (buffer) {
                _device.destroyBuffer(buffer);
                buffer = {};
            }
            _buffer_capacities[I] = sizeof(T) * mesh.vertices<I>().size() *
                                    mesh_buffer_growth_factor;
            _buffer_capacities[I] =
                std::max(_buffer_capacities[I], min_size * sizeof(T));
            buffer = _device.createBuffer(
                vk::BufferCreateInfo()
                    .setSize(_buffer_capacities[I])
                    .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
                    .setSharingMode(vk::SharingMode::eExclusive),
                backend::AllocationCreateInfo()
                    .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_HOST)
                    .setFlags(
                        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                    )
            );
        }
        _buffer_sizes[I] = sizeof(T) * mesh.vertices<I>().size();
    }
    template <size_t I = 0>
    void update_buffers(const Mesh<VertT, Ts...>& mesh) {
        resize_buffer<I>(mesh);
        auto& buffer = _vertex_buffers[I];
        if (buffer) {
            auto data = buffer.map();
            std::memcpy(data, mesh.vertices<I>().data(), _buffer_sizes[I]);
            buffer.unmap();
        }
        if constexpr (I < sizeof...(Ts)) {
            update_buffers<I + 1>(mesh);
        }
    }
    void resize_index_buffer(const Mesh<VertT, Ts...>& mesh) {
        if (mesh.has_indices()) {
            if (mesh.indices16bit()) {
                if (!_index_buffer ||
                    _index_buffer_capacity <
                        mesh.indices16().size() * sizeof(uint16_t)) {
                    if (_index_buffer) {
                        _device.destroyBuffer(_index_buffer);
                        _index_buffer = {};
                    }
                    _index_buffer_capacity = mesh.indices16().size() *
                                             sizeof(uint16_t) *
                                             mesh_buffer_growth_factor;
                    _index_buffer_capacity = std::max(
                        _index_buffer_capacity, min_size * sizeof(uint16_t)
                    );
                    _index_buffer = _device.createBuffer(
                        vk::BufferCreateInfo()
                            .setSize(_index_buffer_capacity)
                            .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
                            .setSharingMode(vk::SharingMode::eExclusive),
                        backend::AllocationCreateInfo()
                            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_HOST)
                            .setFlags(
                                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                            )
                    );
                }
                _index_buffer_size = mesh.indices16().size() * sizeof(uint16_t);
            } else {
                if (!_index_buffer ||
                    _index_buffer_capacity <
                        mesh.indices32().size() * sizeof(uint32_t)) {
                    if (_index_buffer) {
                        _device.destroyBuffer(_index_buffer);
                        _index_buffer = {};
                    }
                    _index_buffer_capacity = mesh.indices32().size() *
                                             sizeof(uint32_t) *
                                             mesh_buffer_growth_factor;
                    _index_buffer_capacity = std::max(
                        _index_buffer_capacity, min_size * sizeof(uint32_t)
                    );
                    _index_buffer = _device.createBuffer(
                        vk::BufferCreateInfo()
                            .setSize(_index_buffer_capacity)
                            .setUsage(
                                vk::BufferUsageFlagBits::eIndexBuffer |
                                vk::BufferUsageFlagBits::eTransferSrc
                            )
                            .setSharingMode(vk::SharingMode::eExclusive),
                        backend::AllocationCreateInfo()
                            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_HOST)
                            .setFlags(
                                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                            )
                    );
                }
                _index_buffer_size = mesh.indices32().size() * sizeof(uint32_t);
            }
        }
    }
    void update_index_buffer(const Mesh<VertT, Ts...>& mesh) {
        resize_index_buffer(mesh);
        if (mesh.has_indices()) {
            auto& buffer = _index_buffer;
            if (buffer) {
                auto data = buffer.map();
                if (mesh.indices16bit()) {
                    std::memcpy(
                        data, mesh.indices16().data(), _index_buffer_size
                    );
                    _index_type = vk::IndexType::eUint16;
                } else {
                    std::memcpy(
                        data, mesh.indices32().data(), _index_buffer_size
                    );
                    _index_type = vk::IndexType::eUint32;
                }
                buffer.unmap();
            }
        }
    }

   public:
    StagingMesh(backend::Device& device, const Mesh<VertT, Ts...>& mesh) {
        this->_device = device;
        update_buffers(mesh);
        update_index_buffer(mesh);
        _vertex_count   = mesh.vertex_count();
        _instance_count = mesh.instance_count();
        _index_count    = mesh.index_count();
    };
    StagingMesh(backend::Device& device) { this->_device = device; };
    void destroy() {
        for (auto& buffer : _vertex_buffers) {
            if (buffer) {
                _device.destroyBuffer(buffer);
                buffer = {};
            }
        }
        if (_index_buffer) {
            _device.destroyBuffer(_index_buffer);
            _index_buffer = {};
        }
    }
    void update(const Mesh<VertT, Ts...>& mesh) {
        update_buffers(mesh);
        update_index_buffer(mesh);
        _vertex_count   = mesh.vertex_count();
        _instance_count = mesh.instance_count();
        _index_count    = mesh.index_count();
    }
    size_t vertex_bindings() const { return sizeof...(Ts) + 1; }
    backend::Buffer vertex_buffer(size_t I = 0) const {
        return _vertex_buffers[I];
    }
    backend::Buffer index_buffer() const { return _index_buffer; }
    vk::IndexType index_type() const { return _index_type; }
    size_t index_buffer_size() const { return _index_buffer_size; }
    size_t vertex_buffer_size(size_t I = 0) const { return _buffer_sizes[I]; }

    std::optional<size_t> vertex_count() const { return _vertex_count; }
    std::optional<size_t> instance_count() const { return _instance_count; }
    std::optional<size_t> index_count() const { return _index_count; }

    friend struct GPUMesh<StagingMesh<Mesh<VertT, Ts...>>>;
};
template <typename VertT, typename... Ts>
struct GPUMesh<Mesh<VertT, Ts...>> {
    using types = std::tuple<VertT, Ts...>;

   private:
    backend::Device _device;
    std::array<backend::Buffer, sizeof...(Ts) + 1> _vertex_buffers;
    std::array<size_t, sizeof...(Ts) + 1> _buffer_sizes;
    std::array<size_t, sizeof...(Ts) + 1> _buffer_capacities;
    backend::Buffer _index_buffer = {};
    vk::IndexType _index_type     = vk::IndexType::eUint16;
    size_t _index_buffer_size     = 0;
    size_t _index_buffer_capacity = 0;

    std::optional<size_t> _vertex_count;
    std::optional<size_t> _instance_count;
    std::optional<size_t> _index_count;

    template <size_t I = 0>
    void resize_buffer(const Mesh<VertT, Ts...>& mesh) {
        using T      = std::tuple_element_t<I, types>;
        auto& buffer = _vertex_buffers[I];
        if (!buffer ||
            _buffer_capacities[I] < sizeof(T) * mesh.vertices<I>().size()) {
            if (buffer) {
                _device.destroyBuffer(buffer);
                buffer = {};
            }
            _buffer_capacities[I] = sizeof(T) * mesh.vertices<I>().size() *
                                    mesh_buffer_growth_factor;
            _buffer_capacities[I] =
                std::max(_buffer_capacities[I], min_size * sizeof(T));
            buffer = _device.createBuffer(
                vk::BufferCreateInfo()
                    .setSize(_buffer_capacities[I])
                    .setUsage(vk::BufferUsageFlagBits::eVertexBuffer)
                    .setSharingMode(vk::SharingMode::eExclusive),
                backend::AllocationCreateInfo()
                    .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                    .setFlags(
                        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                    )
            );
        }
        _buffer_sizes[I] = sizeof(T) * mesh.vertices<I>().size();
    }
    template <size_t I = 0>
    void update_buffers(const Mesh<VertT, Ts...>& mesh) {
        resize_buffer<I>(mesh);
        auto& buffer = _vertex_buffers[I];
        if (buffer) {
            auto data = buffer.map();
            std::memcpy(data, mesh.vertices<I>().data(), _buffer_sizes[I]);
            buffer.unmap();
        }
        if constexpr (I < sizeof...(Ts)) {
            update_buffers<I + 1>(mesh);
        }
    }
    void resize_index_buffer(const Mesh<VertT, Ts...>& mesh) {
        if (mesh.has_indices()) {
            if (mesh.indices16bit()) {
                if (!_index_buffer ||
                    _index_buffer_capacity <
                        mesh.indices16().size() * sizeof(uint16_t)) {
                    if (_index_buffer) {
                        _device.destroyBuffer(_index_buffer);
                        _index_buffer = {};
                    }
                    _index_buffer_capacity = mesh.indices16().size() *
                                             sizeof(uint16_t) *
                                             mesh_buffer_growth_factor;
                    _index_buffer_capacity = std::max(
                        _index_buffer_capacity, min_size * sizeof(uint16_t)
                    );
                    _index_buffer = _device.createBuffer(
                        vk::BufferCreateInfo()
                            .setSize(_index_buffer_capacity)
                            .setUsage(vk::BufferUsageFlagBits::eIndexBuffer)
                            .setSharingMode(vk::SharingMode::eExclusive),
                        backend::AllocationCreateInfo()
                            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                            .setFlags(
                                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                            )
                    );
                }
                _index_buffer_size = mesh.indices16().size() * sizeof(uint16_t);
            } else {
                if (!_index_buffer ||
                    _index_buffer_capacity < mesh.indices32().size()) {
                    if (_index_buffer) {
                        _device.destroyBuffer(_index_buffer);
                        _index_buffer = {};
                    }
                    _index_buffer_capacity = mesh.indices32().size() *
                                             sizeof(uint32_t) *
                                             mesh_buffer_growth_factor;
                    _index_buffer_capacity = std::max(
                        _index_buffer_capacity, min_size * sizeof(uint32_t)
                    );
                    _index_buffer = _device.createBuffer(
                        vk::BufferCreateInfo()
                            .setSize(_index_buffer_capacity)
                            .setUsage(vk::BufferUsageFlagBits::eIndexBuffer)
                            .setSharingMode(vk::SharingMode::eExclusive),
                        backend::AllocationCreateInfo()
                            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                            .setFlags(
                                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                            )
                    );
                }
                _index_buffer_size = mesh.indices32().size() * sizeof(uint32_t);
            }
        }
    }
    void update_index_buffer(const Mesh<VertT, Ts...>& mesh) {
        resize_index_buffer(mesh);
        if (mesh.has_indices()) {
            auto& buffer = _index_buffer;
            if (buffer) {
                auto data = buffer.map();
                if (mesh.indices16bit()) {
                    std::memcpy(
                        data, mesh.indices16().data(), _index_buffer_size
                    );
                    _index_type = vk::IndexType::eUint16;
                } else {
                    std::memcpy(
                        data, mesh.indices32().data(), _index_buffer_size
                    );
                    _index_type = vk::IndexType::eUint32;
                }
                buffer.unmap();
            }
        }
    }

   public:
    GPUMesh(backend::Device& device, const Mesh<VertT, Ts...>& mesh) {
        this->_device = device;
        update_buffers(mesh);
        update_index_buffer(mesh);
        _vertex_count   = mesh.vertex_count();
        _instance_count = mesh.instance_count();
        _index_count    = mesh.index_count();
    };
    GPUMesh(backend::Device& device) { this->_device = device; };
    void destroy() {
        for (auto& buffer : _vertex_buffers) {
            if (buffer) {
                _device.destroyBuffer(buffer);
                buffer = {};
            }
        }
        if (_index_buffer) {
            _device.destroyBuffer(_index_buffer);
            _index_buffer = {};
        }
    }
    void update(const Mesh<VertT, Ts...>& mesh) {
        update_buffers(mesh);
        update_index_buffer(mesh);
        _vertex_count   = mesh.vertex_count();
        _instance_count = mesh.instance_count();
        _index_count    = mesh.index_count();
    }
    size_t vertex_bindings() const { return sizeof...(Ts) + 1; }
    backend::Buffer vertex_buffer(size_t I = 0) const {
        return _vertex_buffers[I];
    }
    auto&& vertex_buffers() { return _vertex_buffers; }
    auto&& vertex_buffers() const { return _vertex_buffers; }
    backend::Buffer index_buffer() const { return _index_buffer; }
    size_t index_buffer_size() const { return _index_buffer_size; }
    size_t vertex_buffer_size(size_t I = 0) const { return _buffer_sizes[I]; }

    std::optional<size_t> vertex_count() const { return _vertex_count; }
    std::optional<size_t> instance_count() const { return _instance_count; }
    std::optional<size_t> index_count() const { return _index_count; }
};
template <typename VertT, typename... Ts>
struct GPUMesh<StagingMesh<Mesh<VertT, Ts...>>> {
    using types = std::tuple<VertT, Ts...>;

   private:
    backend::Device _device;
    std::array<backend::Buffer, sizeof...(Ts) + 1> _vertex_buffers;
    std::array<size_t, sizeof...(Ts) + 1> _buffer_sizes;
    std::array<size_t, sizeof...(Ts) + 1> _buffer_capacities;
    backend::Buffer _index_buffer = {};
    vk::IndexType _index_type     = vk::IndexType::eUint16;
    size_t _index_buffer_size     = 0;
    size_t _index_buffer_capacity = 0;

    std::optional<size_t> _vertex_count;
    std::optional<size_t> _instance_count;
    std::optional<size_t> _index_count;

    template <size_t I = 0>
    void resize_buffer(const StagingMesh<Mesh<VertT, Ts...>>& mesh) {
        using T      = std::tuple_element_t<I, types>;
        auto& buffer = _vertex_buffers[I];
        if (!buffer || _buffer_capacities[I] < mesh._buffer_sizes[I]) {
            if (buffer) {
                _device.destroyBuffer(buffer);
                buffer = {};
            }
            _buffer_capacities[I] =
                mesh._buffer_sizes[I] * mesh_buffer_growth_factor;
            _buffer_capacities[I] =
                std::max(_buffer_capacities[I], min_size * sizeof(T));
            buffer = _device.createBuffer(
                vk::BufferCreateInfo()
                    .setSize(_buffer_capacities[I])
                    .setUsage(
                        vk::BufferUsageFlagBits::eVertexBuffer |
                        vk::BufferUsageFlagBits::eTransferDst
                    )
                    .setSharingMode(vk::SharingMode::eExclusive),
                backend::AllocationCreateInfo()
                    .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                    .setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)
            );
        }
        _buffer_sizes[I] = mesh._buffer_sizes[I];
    }
    template <size_t I = 0>
    void update_buffers(
        const StagingMesh<Mesh<VertT, Ts...>>& mesh, backend::CommandBuffer& cmd
    ) {
        auto& buffer = _vertex_buffers[I];
        resize_buffer<I>(mesh);
        if (buffer && _buffer_sizes[I] > 0) {
            cmd.copyBuffer(
                mesh.vertex_buffer(I), buffer,
                vk::BufferCopy().setSize(_buffer_sizes[I])
            );
        }
        if constexpr (I < sizeof...(Ts)) {
            update_buffers<I + 1>(mesh);
        }
    }
    void resize_index_buffer(const StagingMesh<Mesh<VertT, Ts...>>& mesh) {
        if (mesh._index_buffer) {
            if (!_index_buffer ||
                _index_buffer_capacity < mesh._index_buffer_size) {
                if (_index_buffer) {
                    _device.destroyBuffer(_index_buffer);
                    _index_buffer = {};
                }
                _index_buffer_capacity =
                    mesh._index_buffer_size * mesh_buffer_growth_factor;
                _index_buffer_capacity = std::max(
                    _index_buffer_capacity,
                    min_size * (_index_type == vk::IndexType::eUint16
                                    ? sizeof(uint16_t)
                                    : sizeof(uint32_t))
                );
                _index_buffer = _device.createBuffer(
                    vk::BufferCreateInfo()
                        .setSize(_index_buffer_capacity)
                        .setUsage(
                            vk::BufferUsageFlagBits::eIndexBuffer |
                            vk::BufferUsageFlagBits::eTransferDst
                        )
                        .setSharingMode(vk::SharingMode::eExclusive),
                    backend::AllocationCreateInfo()
                        .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                        .setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)
                );
            }
            _index_buffer_size = mesh._index_buffer_size;
        }
    }
    void update_index_buffer(
        const StagingMesh<Mesh<VertT, Ts...>>& mesh, backend::CommandBuffer& cmd
    ) {
        resize_index_buffer(mesh);
        if (mesh._index_buffer && _index_buffer_size > 0) {
            auto& buffer = _index_buffer;
            if (buffer) {
                cmd.copyBuffer(
                    mesh.index_buffer(), buffer,
                    vk::BufferCopy().setSize(_index_buffer_size)
                );
                _index_type = mesh.index_type();
            }
        }
    }

   public:
    GPUMesh(backend::Device& device) { this->_device = device; };
    void destroy() {
        for (auto& buffer : _vertex_buffers) {
            if (buffer) {
                _device.destroyBuffer(buffer);
                buffer = {};
            }
        }
        if (_index_buffer) {
            _device.destroyBuffer(_index_buffer);
            _index_buffer = {};
        }
    }
    void update(
        const StagingMesh<Mesh<VertT, Ts...>>& mesh, backend::CommandBuffer& cmd
    ) {
        update_buffers(mesh, cmd);
        update_index_buffer(mesh, cmd);
        _vertex_count   = mesh.vertex_count();
        _instance_count = mesh.instance_count();
        _index_count    = mesh.index_count();
    }
    size_t vertex_bindings() const { return sizeof...(Ts) + 1; }
    backend::Buffer vertex_buffer(size_t I = 0) const {
        return _vertex_buffers[I];
    }
    auto&& vertex_buffers() { return _vertex_buffers; }
    auto&& vertex_buffers() const { return _vertex_buffers; }
    backend::Buffer index_buffer() const { return _index_buffer; }
    vk::IndexType index_type() const { return _index_type; }
    size_t index_buffer_size() const { return _index_buffer_size; }
    size_t vertex_buffer_size(size_t I = 0) const { return _buffer_sizes[I]; }

    std::optional<size_t> vertex_count() const { return _vertex_count; }
    std::optional<size_t> instance_count() const { return _instance_count; }
    std::optional<size_t> index_count() const { return _index_count; }
};

struct Subpass;
struct PassBase;
struct Pass;

struct PipelineBase {
   public:
    EPIX_API PipelineBase();
    EPIX_API void create();
    EPIX_API void destroy();

    EPIX_API PipelineBase& set_descriptor_pool(
        std::function<backend::DescriptorPool(backend::Device&)> func
    );
    EPIX_API PipelineBase& set_vertex_attributes(
        std::function<std::vector<vk::VertexInputAttributeDescription>()> func
    );
    EPIX_API PipelineBase& set_vertex_bindings(
        std::function<std::vector<vk::VertexInputBindingDescription>()> func
    );
    EPIX_API PipelineBase& set_input_assembly_state(
        std::function<vk::PipelineInputAssemblyStateCreateInfo()> func
    );
    EPIX_API PipelineBase& set_default_topology(vk::PrimitiveTopology topology);
    EPIX_API PipelineBase& set_rasterization_state(
        std::function<vk::PipelineRasterizationStateCreateInfo()> func
    );
    EPIX_API PipelineBase& set_multisample_state(
        std::function<vk::PipelineMultisampleStateCreateInfo()> func
    );
    EPIX_API PipelineBase& set_depth_stencil_state(
        std::function<vk::PipelineDepthStencilStateCreateInfo()> func
    );
    EPIX_API PipelineBase& set_color_blend_attachments(
        std::function<std::vector<vk::PipelineColorBlendAttachmentState>()> func
    );
    EPIX_API PipelineBase& set_color_blend_state(
        std::function<vk::PipelineColorBlendStateCreateInfo()> func
    );
    EPIX_API PipelineBase& set_dynamic_states(
        std::function<std::vector<vk::DynamicState>()> func
    );

    template <typename... Args>
        requires(std::constructible_from<std::vector<uint32_t>, Args...>)
    PipelineBase& add_shader(vk::ShaderStageFlagBits stage, Args... args) {
        shader_sources.emplace_back(stage, std::vector<uint32_t>{args...});
        return *this;
    }

   private:
    backend::Device device;
    backend::RenderPass render_pass;
    uint32_t subpass_index = -1;
    backend::Pipeline pipeline;
    backend::PipelineLayout pipeline_layout;
    std::vector<backend::DescriptorSetLayout> descriptor_set_layouts;
    backend::DescriptorPool descriptor_pool;

    vk::ShaderStageFlags push_constant_stage;

    std::vector<std::pair<vk::ShaderStageFlagBits, std::vector<uint32_t>>>
        shader_sources;

    std::function<backend::DescriptorPool(backend::Device&)>
        func_create_descriptor_pool;

    std::function<std::vector<vk::VertexInputBindingDescription>()>
        func_vertex_input_bindings;
    std::function<std::vector<vk::VertexInputAttributeDescription>()>
        func_vertex_input_attributes;

    std::function<vk::PipelineInputAssemblyStateCreateInfo()>
        func_input_assembly_state;
    std::optional<vk::PrimitiveTopology> default_topology;
    std::function<vk::PipelineRasterizationStateCreateInfo()>
        func_rasterization_state;
    std::function<vk::PipelineMultisampleStateCreateInfo()>
        func_multisample_state;
    std::function<vk::PipelineDepthStencilStateCreateInfo()>
        func_depth_stencil_state;
    std::function<std::vector<vk::PipelineColorBlendAttachmentState>()>
        func_color_blend_attachments;
    std::function<vk::PipelineColorBlendStateCreateInfo()>
        func_color_blend_state;
    std::function<std::vector<vk::DynamicState>()> func_dynamic_states;

    EPIX_API void create_descriptor_pool();
    EPIX_API void create_layout();
    EPIX_API void create_pipeline();

    friend struct Subpass;
    friend struct PassBase;
    friend struct Pass;
};

struct PassBase {
    struct SubpassInfo {
        vk::PipelineBindPoint bind_point = vk::PipelineBindPoint::eGraphics;
        std::vector<vk::AttachmentReference> color_attachments;
        std::optional<vk::AttachmentReference> depth_attachment;
        std::vector<vk::AttachmentReference> resolve_attachment;
        std::vector<vk::AttachmentReference> input_attachments;
        std::vector<uint32_t> preserve_attachments;

        EPIX_API SubpassInfo& set_bind_point(vk::PipelineBindPoint bind_point);
        EPIX_API SubpassInfo& set_colors(
            const vk::ArrayProxy<const vk::AttachmentReference>& attachments
        );
        EPIX_API SubpassInfo& set_depth(vk::AttachmentReference attachment);
        EPIX_API SubpassInfo& set_resolves(
            const vk::ArrayProxy<const vk::AttachmentReference>& attachments
        );
        EPIX_API SubpassInfo& set_inputs(
            const vk::ArrayProxy<const vk::AttachmentReference>& attachments
        );
        EPIX_API SubpassInfo& set_preserves(
            const vk::ArrayProxy<const uint32_t>& attachments
        );
    };

   protected:
    backend::Device _device;
    backend::RenderPass _render_pass;
    std::vector<vk::AttachmentDescription> _attachments;

    std::vector<SubpassInfo> _subpasses;
    std::vector<vk::SubpassDependency> _dependencies;

    std::vector<std::vector<std::unique_ptr<PipelineBase>>> _pipelines;
    std::vector<entt::dense_map<std::string, uint32_t>> _pipeline_maps;

    EPIX_API PassBase(backend::Device& device);
    EPIX_API void create();

   public:
    EPIX_API static PassBase* create_new(
        backend::Device& device, std::function<void(PassBase&)> pass_setup
    );
    EPIX_API static std::unique_ptr<PassBase> create_unique(
        backend::Device& device, std::function<void(PassBase&)> pass_setup
    );
    EPIX_API static std::shared_ptr<PassBase> create_shared(
        backend::Device& device, std::function<void(PassBase&)> pass_setup
    );
    EPIX_API static PassBase* create_simple(backend::Device& device);
    EPIX_API static PassBase* create_simple_depth(backend::Device& device);

    EPIX_API void set_attachments(
        const vk::ArrayProxy<const vk::AttachmentDescription>& attachments
    );
    EPIX_API void set_dependencies(
        const vk::ArrayProxy<const vk::SubpassDependency>& dependencies
    );

    EPIX_API SubpassInfo& subpass_info(uint32_t index);

    EPIX_API uint32_t add_pipeline(
        uint32_t subpass,
        const std::string& name,
        std::function<void(PipelineBase&)> pipeline_setup
    );

    EPIX_API uint32_t add_pipeline(
        uint32_t subpass, const std::string& name, PipelineBase* pipeline
    );

    EPIX_API uint32_t
    pipeline_index(uint32_t subpass, const std::string& name) const;

    EPIX_API PipelineBase* get_pipeline(uint32_t subpass, uint32_t index) const;

    EPIX_API PipelineBase* get_pipeline(
        uint32_t subpass, const std::string& name
    ) const;

    EPIX_API void destroy();

    EPIX_API uint32_t subpass_count() const;

    friend struct Pass;
    friend struct Subpass;
};

struct Subpass {
   protected:
    backend::CommandBuffer _cmd;

    backend::Device _device;
    std::vector<const PipelineBase*> _pipelines;
    std::vector<std::vector<vk::Viewport>> _viewports;
    std::vector<std::vector<vk::Rect2D>> _scissors;
    std::vector<std::vector<backend::DescriptorSet>> _descriptor_sets;
    std::vector<std::function<
        void(backend::Device&, const backend::DescriptorPool&, std::vector<backend::DescriptorSet>&)>>
        _funcs_destroy_desc_set;
    uint32_t _active_pipeline = -1;

   public:
    Subpass() = default;

    EPIX_API void activate_pipeline(
        uint32_t index,
        std::function<
            void(std::vector<vk::Viewport>&, std::vector<vk::Rect2D>&)>
            func_viewport_scissor,
        std::function<
            void(backend::Device&, std::vector<backend::DescriptorSet>&)>
            func_desc = {}
    );

    EPIX_API uint32_t add_pipeline(
        const PipelineBase* pipeline,
        std::function<std::vector<
            backend::
                DescriptorSet>(backend::Device&, const backend::DescriptorPool&, const std::vector<backend::DescriptorSetLayout>&)>
            func_desc_set = {},
        std::function<
            void(backend::Device&, const backend::DescriptorPool&, std::vector<backend::DescriptorSet>&)>
            func_destroy_desc_set = {}
    );

    EPIX_API Subpass(Pass& pass);
    EPIX_API void destroy();

    EPIX_API void begin(backend::CommandBuffer& cmd);
    EPIX_API void task(std::function<void(backend::CommandBuffer&)> func);

    template <typename MeshTs>
        requires requires(GPUMesh<MeshTs> mesh) {
            mesh.vertex_buffers();
            mesh.index_buffer();
            mesh.index_count();
            mesh.vertex_count();
            mesh.instance_count();
        }
    void draw(const GPUMesh<MeshTs>& mesh) {
        if (_active_pipeline == -1) {
            spdlog::warn("Subpass::draw called without activating pipeline");
            return;
        }
        std::vector<vk::DeviceSize> offsets(mesh.vertex_buffers().size(), 0);
        std::vector<vk::Buffer> buffers;
        buffers.reserve(mesh.vertex_buffers().size());
        for (auto& buffer : mesh.vertex_buffers()) {
            buffers.push_back(buffer.buffer);
        }
        _cmd.bindVertexBuffers(0, buffers, offsets);
        if (mesh.index_buffer() && mesh.index_count()) {
            _cmd.bindIndexBuffer(
                mesh.index_buffer().buffer, 0, mesh.index_type()
            );
            _cmd.drawIndexed(
                mesh.index_count().value(), mesh.instance_count().value(), 0, 0,
                0
            );
        } else {
            _cmd.draw(
                mesh.vertex_count().value(), mesh.instance_count().value(), 0, 0
            );
        }
    }
    template <typename MeshTs, typename PushConstantT>
        requires requires(GPUMesh<MeshTs> mesh) {
            mesh.vertex_buffers();
            mesh.index_buffer();
            mesh.index_count();
            mesh.vertex_count();
            mesh.instance_count();
        }
    void draw(const GPUMesh<MeshTs>& mesh, const PushConstantT& push_constant) {
        if (_active_pipeline == -1) {
            spdlog::warn("Subpass::draw called without activating pipeline");
            return;
        }
        auto pipeline = _pipelines[_active_pipeline];
        std::vector<vk::DeviceSize> offsets(mesh.vertex_buffers().size(), 0);
        std::vector<vk::Buffer> buffers;
        buffers.reserve(mesh.vertex_buffers().size());
        for (auto& buffer : mesh.vertex_buffers()) {
            buffers.push_back(buffer.buffer);
        }
        if (pipeline->push_constant_stage != vk::ShaderStageFlags(0)) {
            _cmd.pushConstants(
                pipeline->pipeline_layout, pipeline->push_constant_stage, 0,
                sizeof(PushConstantT), &push_constant
            );
        } else {
            spdlog::warn(
                "No push constant block for in this pipeline, ignoring "
                "provided push constant data."
            );
        }
        _cmd.bindVertexBuffers(0, buffers, offsets);
        if (mesh.index_buffer() && mesh.index_count()) {
            _cmd.bindIndexBuffer(
                mesh.index_buffer().buffer, 0, mesh.index_type()
            );
            _cmd.drawIndexed(
                mesh.index_count().value(), mesh.instance_count().value(), 0, 0,
                0
            );
        } else {
            _cmd.draw(
                mesh.vertex_count().value(), mesh.instance_count().value(), 0, 0
            );
        }
    }

    friend struct Pass;
};

struct Pass {
   protected:
    backend::Device _device;
    backend::CommandPool _cmd_pool;
    backend::RenderPass _render_pass;
    const uint32_t _subpass_count;
    std::vector<Subpass> _subpasses;
    const PassBase* _base;

    backend::CommandBuffer _cmd;
    backend::Fence _fence;

    backend::Framebuffer _framebuffer;
    vk::Extent2D _extent;

    bool recording           = false;
    bool in_render_pass      = false;
    bool ready               = false;
    uint32_t current_subpass = 0;

   public:
    EPIX_API Pass(
        const PassBase* base,
        backend::CommandPool& command_pool,
        std::function<void(Pass&, const PassBase&)> subpass_setup
    );

    EPIX_API void destroy();

    EPIX_API Pass& add_subpass(
        uint32_t index,
        std::function<void(const PassBase&, Pass&, Subpass&)> subpass_setup = {}
    );

    EPIX_API uint32_t subpass_add_pipeline(
        uint32_t subpass_index,
        uint32_t pipeline_index,
        std::function<std::vector<
            backend::
                DescriptorSet>(backend::Device&, const backend::DescriptorPool&, const std::vector<backend::DescriptorSetLayout>&)>
            func_desc_set = {},
        std::function<
            void(backend::Device&, const backend::DescriptorPool&, std::vector<backend::DescriptorSet>&)>
            func_destroy_desc_set = {}
    );

    EPIX_API uint32_t subpass_add_pipeline(
        uint32_t subpass_index,
        const std::string& pipeline_name,
        std::function<std::vector<
            backend::
                DescriptorSet>(backend::Device&, const backend::DescriptorPool&, const std::vector<backend::DescriptorSetLayout>&)>
            func_desc_set = {},
        std::function<
            void(backend::Device&, const backend::DescriptorPool&, std::vector<backend::DescriptorSet>&)>
            func_destroy_desc_set = {}
    );

    EPIX_API void begin(
        std::function<
            backend::Framebuffer(backend::Device&, backend::RenderPass&)> func,
        vk::Extent2D extent
    );

    template <typename... Verts>
    void update_mesh(
        GPUMesh<StagingMesh<Verts...>>& dst, const StagingMesh<Verts...>& src
    ) {
        if (!recording) {
            spdlog::warn(
                "Pass::update_mesh called outside of recording context."
            );
            return;
        }
        if (in_render_pass) {
            spdlog::warn("Pass::update_mesh called inside of render pass.");
            return;
        }
        dst.update(src, _cmd);
    }

    EPIX_API Subpass& next_subpass();
    EPIX_API void end();
    EPIX_API void submit(backend::Queue& queue);

    friend struct Subpass;
};
}  // namespace epix::render::vulkan2