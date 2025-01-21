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
    Mesh(Args... args) : input_rate_instance(args...) {}
    Mesh() : input_rate_instance(false) {}

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
    bool has_indices() const { return !std::holds_alternative<void*>(indices); }
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
            buffer = _device.createBuffer(
                vk::BufferCreateInfo()
                    .setSize(_buffer_capacities[I])
                    .setUsage(
                        vk::BufferUsageFlagBits::eVertexBuffer |
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
        _buffer_sizes[I] = sizeof(T) * mesh.vertices<I>().size();
    }
    template <size_t I = 0>
    void update_buffers(const Mesh<VertT, Ts...>& mesh) {
        auto& buffer = _vertex_buffers[I];
        resize_buffer<I>(mesh);
        auto data = buffer.map();
        std::memcpy(data, mesh.vertices<I>().data(), _buffer_sizes[I]);
        buffer.unmap();
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
        auto& buffer = _vertex_buffers[I];
        resize_buffer<I>(mesh);
        auto data = buffer.map();
        std::memcpy(data, mesh.vertices<I>().data(), _buffer_sizes[I]);
        buffer.unmap();
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
            auto data    = buffer.map();
            if (mesh.indices16bit()) {
                std::memcpy(data, mesh.indices16().data(), _index_buffer_size);
                _index_type = vk::IndexType::eUint16;
            } else {
                std::memcpy(data, mesh.indices32().data(), _index_buffer_size);
                _index_type = vk::IndexType::eUint32;
            }
            buffer.unmap();
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
        cmd.copyBuffer(
            mesh.vertex_buffer(I), buffer,
            vk::BufferCopy().setSize(_buffer_sizes[I])
        );
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
        if (mesh._index_buffer) {
            auto& buffer = _index_buffer;
            cmd.copyBuffer(
                mesh.index_buffer(), buffer,
                vk::BufferCopy().setSize(_index_buffer_size)
            );
            _index_type = mesh.index_type();
        }
    }

   public:
    GPUMesh(
        backend::Device& device, const StagingMesh<Mesh<VertT, Ts...>>& mesh
    ) {
        this->_device = device;
        update_buffers(mesh);
        update_index_buffer(mesh);
        _vertex_count   = mesh._vertex_count;
        _instance_count = mesh._instance_count;
        _index_count    = mesh._index_count;
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
    backend::Buffer index_buffer() const { return _index_buffer; }
    vk::IndexType index_type() const { return _index_type; }
    size_t index_buffer_size() const { return _index_buffer_size; }
    size_t vertex_buffer_size(size_t I = 0) const { return _buffer_sizes[I]; }

    std::optional<size_t> vertex_count() const { return _vertex_count; }
    std::optional<size_t> instance_count() const { return _instance_count; }
    std::optional<size_t> index_count() const { return _index_count; }
};
template <typename MeshT, typename PushConstantT>
struct Batch {};
struct PipelineBase {
   public:
    EPIX_API PipelineBase(backend::Device& device);
    EPIX_API void create();
    EPIX_API void destroy();

    EPIX_API PipelineBase& set_render_pass(
        std::function<backend::RenderPass(backend::Device&)> func
    );
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
    backend::Pipeline pipeline;
    backend::PipelineLayout pipeline_layout;
    std::vector<backend::DescriptorSetLayout> descriptor_set_layouts;
    backend::DescriptorPool descriptor_pool;

    vk::ShaderStageFlags push_constant_stage;

    std::vector<std::pair<vk::ShaderStageFlagBits, std::vector<uint32_t>>>
        shader_sources;

    std::function<backend::RenderPass(backend::Device&)>
        func_create_render_pass;
    std::function<backend::DescriptorPool(backend::Device&)>
        func_create_descriptor_pool;

    std::function<std::vector<vk::VertexInputBindingDescription>()>
        func_vertex_input_bindings;
    std::function<std::vector<vk::VertexInputAttributeDescription>()>
        func_vertex_input_attributes;

    std::function<vk::PipelineInputAssemblyStateCreateInfo()>
        func_input_assembly_state;
    vk::PrimitiveTopology default_topology;
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

    EPIX_API void create_render_pass();
    EPIX_API void create_descriptor_pool();
    EPIX_API void create_layout();
    EPIX_API void create_pipeline(uint32_t subpass = 0);

    template <typename MeshT, typename PushConstantT>
    friend struct Batch;
};
template <typename... Ts, typename PushConstantT>
struct Batch<Mesh<Ts...>, PushConstantT> {
    using mesh_t           = Mesh<Ts...>;
    using staging_mesh_t   = StagingMesh<mesh_t>;
    using gpu_mesh_t       = GPUMesh<mesh_t>;
    using dedicated_mesh_t = GPUMesh<staging_mesh_t>;

   public:
    void update_descriptor_sets(
        std::function<void(std::vector<backend::DescriptorSet>&)> func
    ) {
        if (!rendering) {
            spdlog::warn(
                "Batch::update_descriptor_sets called outside of "
                "rendering context"
            );
            return;
        }
        func(_descriptor_sets);
    }
    void update_descriptor_sets(
        std::function<
            void(backend::Device&, std::vector<backend::DescriptorSet>&)> func
    ) {
        if (!rendering) {
            spdlog::warn(
                "Batch::update_descriptor_sets called outside of "
                "rendering context"
            );
            return;
        }
        func(_device, _descriptor_sets);
    }
    void update_descriptor_sets(
        std::vector<vk::WriteDescriptorSet> writes,
        std::vector<vk::CopyDescriptorSet> copies = {}
    ) {
        if (!rendering) {
            spdlog::warn(
                "Batch::update_descriptor_sets called outside of "
                "rendering context"
            );
            return;
        }
        _device.updateDescriptorSets(writes, copies);
    }
    void begin(
        std::function<
            backend::Framebuffer(backend::Device&, backend::RenderPass&)> func,
        vk::Extent2D extent,
        std::function<void(std::vector<backend::DescriptorSet>&)> func_desc = {}
    ) {
        _device.waitForFences(_fence, true, UINT64_MAX);
        _device.resetFences(_fence);
        if (_framebuffer) {
            _device.destroyFramebuffer(_framebuffer);
        }
        _framebuffer = func(_device, _render_pass);
        _extent      = extent;
        rendering    = true;
        if (func_desc) {
            func_desc(_descriptor_sets);
        }
        _command_buffer.reset();
        _command_buffer.begin(vk::CommandBufferBeginInfo());
    }
    void draw(const staging_mesh_t& mesh, const PushConstantT& push_constant) {
        if (!rendering) {
            spdlog::warn("Batch::draw called outside of rendering context");
            return;
        }
        if (!mesh.vertex_count()) {
            spdlog::warn(
                "Batch::draw called with invalid mesh, vertex count in "
                "different bindings may be different"
            );
            return;
        }
        if (!mesh.instance_count()) {
            spdlog::warn(
                "Batch::draw called with invalid mesh, instance count in "
                "different bindings may be different"
            );
            return;
        }
        _mesh.update(mesh, _command_buffer);
        draw(_mesh, push_constant);
    }
    void draw(const gpu_mesh_t& mesh, const PushConstantT& push_constant) {
        if (!rendering) {
            spdlog::warn("Batch::draw called outside of rendering context");
            return;
        }
        if (!mesh.vertex_count()) {
            spdlog::warn(
                "Batch::draw called with invalid mesh, vertex count in "
                "different bindings may be different"
            );
            return;
        }
        if (!mesh.instance_count()) {
            spdlog::warn(
                "Batch::draw called with invalid mesh, instance count in "
                "different bindings may be different"
            );
            return;
        }
        begin_pipeline();
        _command_buffer.pushConstants(
            _pipeline_layout, _push_constant_stage, 0, sizeof(PushConstantT),
            &push_constant
        );
        std::vector<vk::Buffer> vertex_buffers;
        std::vector<vk::DeviceSize> offsets;
        vertex_buffers.reserve(mesh.vertex_bindings());
        offsets.reserve(mesh.vertex_bindings());
        for (size_t i = 0; i < mesh.vertex_bindings(); i++) {
            vertex_buffers.push_back(mesh.vertex_buffer(i).buffer);
            offsets.push_back(0);
        }
        _command_buffer.bindVertexBuffers(0, vertex_buffers, offsets);
        if (mesh.index_buffer() && mesh.index_count()) {
            vk::Buffer index_buffer = mesh.index_buffer().buffer;
            _command_buffer.bindIndexBuffer(index_buffer, 0, mesh.index_type());
            _command_buffer.drawIndexed(
                mesh.index_count().value(), mesh.instance_count().value(), 0, 0,
                0
            );
        } else {
            _command_buffer.draw(
                mesh.vertex_count().value(), mesh.instance_count().value(), 0, 0
            );
        }
        end_pipeline();
    }
    void draw(
        const dedicated_mesh_t& mesh, const PushConstantT& push_constant
    ) {
        if (!rendering) {
            spdlog::warn("Batch::draw called outside of rendering context");
            return;
        }
        if (!mesh.vertex_count()) {
            spdlog::warn(
                "Batch::draw called with invalid mesh, vertex count in "
                "different bindings may be different"
            );
            return;
        }
        if (!mesh.instance_count()) {
            spdlog::warn(
                "Batch::draw called with invalid mesh, instance count in "
                "different bindings may be different"
            );
            return;
        }
        begin_pipeline();
        _command_buffer.pushConstants(
            _pipeline_layout, _push_constant_stage, 0, sizeof(PushConstantT),
            &push_constant
        );
        std::vector<vk::Buffer> vertex_buffers;
        std::vector<vk::DeviceSize> offsets;
        vertex_buffers.reserve(mesh.vertex_bindings());
        offsets.reserve(mesh.vertex_bindings());
        for (size_t i = 0; i < mesh.vertex_bindings(); i++) {
            vertex_buffers.push_back(mesh.vertex_buffer(i).buffer);
            offsets.push_back(0);
        }
        _command_buffer.bindVertexBuffers(0, vertex_buffers, offsets);
        if (mesh.index_buffer() && mesh.index_count()) {
            vk::Buffer index_buffer = mesh.index_buffer().buffer;
            _command_buffer.bindIndexBuffer(index_buffer, 0, mesh.index_type());
            _command_buffer.drawIndexed(
                mesh.index_count().value(), mesh.instance_count().value(), 0, 0,
                0
            );
        } else {
            _command_buffer.draw(
                mesh.vertex_count().value(), mesh.instance_count().value(), 0, 0
            );
        }
        end_pipeline();
    }
    void end(backend::Queue& queue) {
        _command_buffer.end();
        rendering = false;
        queue.submit(
            vk::SubmitInfo().setCommandBuffers(_command_buffer), _fence
        );
    }

    Batch(PipelineBase& pipeline, backend::CommandPool& command_pool)
        : Batch(
              pipeline.device,
              command_pool,
              pipeline.render_pass,
              pipeline.pipeline,
              pipeline.pipeline_layout
          ) {
        _push_constant_stage = pipeline.push_constant_stage;
    }

    void destroy() {
        _device.waitForFences(_fence, true, UINT64_MAX);
        if (_framebuffer) {
            _device.destroyFramebuffer(_framebuffer);
        }
        _device.destroyFence(_fence);
        _device.freeCommandBuffers(_command_pool, _command_buffer);
        _mesh.destroy();
    }

   private:
    backend::Device _device;
    backend::CommandPool _command_pool;

    backend::RenderPass _render_pass;
    backend::Pipeline _pipeline;
    backend::PipelineLayout _pipeline_layout;

    vk::ShaderStageFlags _push_constant_stage;

    std::vector<backend::DescriptorSet> _descriptor_sets;

    backend::CommandBuffer _command_buffer;
    backend::Fence _fence;

    backend::Framebuffer _framebuffer;
    vk::Extent2D _extent;

    dedicated_mesh_t _mesh;

    bool rendering = false;

    Batch(
        backend::Device& device,
        backend::CommandPool& command_pool,
        backend::RenderPass& render_pass,
        backend::Pipeline& pipeline,
        backend::PipelineLayout& pipeline_layout
    )
        : _mesh(device) {
        this->_device          = device;
        this->_command_pool    = command_pool;
        this->_render_pass     = render_pass;
        this->_pipeline        = pipeline;
        this->_pipeline_layout = pipeline_layout;
        _command_buffer        = _device.allocateCommandBuffers(
            vk::CommandBufferAllocateInfo()
                .setCommandPool(_command_pool)
                .setCommandBufferCount(1)
                .setLevel(vk::CommandBufferLevel::ePrimary)
        )[0];
        _fence = _device.createFence(
            vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
        );
    }

    void begin_pipeline() {
        vk::RenderPassBeginInfo render_pass_info;
        render_pass_info.setRenderPass(_render_pass);
        render_pass_info.setFramebuffer(_framebuffer);
        render_pass_info.setRenderArea(vk::Rect2D().setExtent(_extent));
        _command_buffer.beginRenderPass(
            render_pass_info, vk::SubpassContents::eInline
        );
        _command_buffer.bindPipeline(
            vk::PipelineBindPoint::eGraphics, _pipeline
        );
        _command_buffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics, _pipeline_layout, 0,
            _descriptor_sets, {}
        );
        _command_buffer.setViewport(
            0, vk::Viewport()
                   .setWidth(_extent.width)
                   .setHeight(_extent.height)
                   .setMaxDepth(1.0f)
        );
        _command_buffer.setScissor(0, vk::Rect2D().setExtent(_extent));
    }
    void end_pipeline() { _command_buffer.endRenderPass(); }
};
template <typename... Ts>
struct Batch<Mesh<Ts...>, void> {
    using mesh_t           = Mesh<Ts...>;
    using staging_mesh_t   = StagingMesh<mesh_t>;
    using gpu_mesh_t       = GPUMesh<mesh_t>;
    using dedicated_mesh_t = GPUMesh<staging_mesh_t>;

   public:
    void update_descriptor_sets(
        std::function<void(std::vector<backend::DescriptorSet>&)> func
    ) {
        if (!rendering) {
            spdlog::warn(
                "Batch::update_descriptor_sets called outside of "
                "rendering context"
            );
            return;
        }
        func(_descriptor_sets);
    }
    void update_descriptor_sets(
        std::function<
            void(backend::Device&, std::vector<backend::DescriptorSet>&)> func
    ) {
        if (!rendering) {
            spdlog::warn(
                "Batch::update_descriptor_sets called outside of "
                "rendering context"
            );
            return;
        }
        func(_device, _descriptor_sets);
    }
    void update_descriptor_sets(
        std::vector<vk::WriteDescriptorSet> writes,
        std::vector<vk::CopyDescriptorSet> copies = {}
    ) {
        if (!rendering) {
            spdlog::warn(
                "Batch::update_descriptor_sets called outside of "
                "rendering context"
            );
            return;
        }
        _device.updateDescriptorSets(writes, copies);
    }
    void begin(
        std::function<
            backend::Framebuffer(backend::Device&, backend::RenderPass&)> func,
        vk::Extent2D extent,
        std::function<void(std::vector<backend::DescriptorSet>&)>
            func_desc_set = {}
    ) {
        _device.waitForFences(_fence, true, UINT64_MAX);
        _device.resetFences(_fence);
        if (_framebuffer) {
            _device.destroyFramebuffer(_framebuffer);
        }
        _framebuffer = func(_device, _render_pass);
        rendering    = true;
        if (func_desc_set) {
            func_desc_set(_descriptor_sets);
        }
        _command_buffer.reset();
        _command_buffer.begin(vk::CommandBufferBeginInfo());
    }
    void draw(const staging_mesh_t& mesh) {
        if (!rendering) {
            spdlog::warn("Batch::draw called outside of rendering context");
            return;
        }
        if (!mesh.vertex_count()) {
            spdlog::warn(
                "Batch::draw called with invalid mesh, vertex count in "
                "different bindings may be different"
            );
            return;
        }
        if (!mesh.instance_count()) {
            spdlog::warn(
                "Batch::draw called with invalid mesh, instance count in "
                "different bindings may be different"
            );
            return;
        }
        _mesh.update(mesh, _command_buffer);
        draw(_mesh);
    }
    void draw(const gpu_mesh_t& mesh) {
        if (!rendering) {
            spdlog::warn("Batch::draw called outside of rendering context");
            return;
        }
        if (!mesh.vertex_count()) {
            spdlog::warn(
                "Batch::draw called with invalid mesh, vertex count in "
                "different bindings may be different"
            );
            return;
        }
        if (!mesh.instance_count()) {
            spdlog::warn(
                "Batch::draw called with invalid mesh, instance count in "
                "different bindings may be different"
            );
            return;
        }
        begin_pipeline();
        std::vector<vk::Buffer> vertex_buffers;
        std::vector<vk::DeviceSize> offsets;
        vertex_buffers.reserve(mesh.vertex_bindings());
        offsets.reserve(mesh.vertex_bindings());
        for (size_t i = 0; i < mesh.vertex_bindings(); i++) {
            vertex_buffers.push_back(mesh.vertex_buffer(i).buffer);
            offsets.push_back(0);
        }
        _command_buffer.bindVertexBuffers(0, vertex_buffers, offsets);
        if (mesh.index_buffer() && mesh.index_count()) {
            vk::Buffer index_buffer = mesh.index_buffer().buffer;
            _command_buffer.bindIndexBuffer(index_buffer, 0, mesh.index_type());
            _command_buffer.drawIndexed(
                mesh.index_count().value(), mesh.instance_count().value(), 0, 0,
                0
            );
        } else {
            _command_buffer.draw(
                mesh.vertex_count().value(), mesh.instance_count().value(), 0, 0
            );
        }
        end_pipeline();
    }
    void draw(const dedicated_mesh_t& mesh) {
        if (!rendering) {
            spdlog::warn("Batch::draw called outside of rendering context");
            return;
        }
        if (!mesh.vertex_count()) {
            spdlog::warn(
                "Batch::draw called with invalid mesh, vertex count in "
                "different bindings may be different"
            );
            return;
        }
        if (!mesh.instance_count()) {
            spdlog::warn(
                "Batch::draw called with invalid mesh, instance count in "
                "different bindings may be different"
            );
            return;
        }
        begin_pipeline();
        std::vector<vk::Buffer> vertex_buffers;
        std::vector<vk::DeviceSize> offsets;
        vertex_buffers.reserve(mesh.vertex_bindings());
        offsets.reserve(mesh.vertex_bindings());
        for (size_t i = 0; i < mesh.vertex_bindings(); i++) {
            vertex_buffers.push_back(mesh.vertex_buffer(i).buffer);
            offsets.push_back(0);
        }
        _command_buffer.bindVertexBuffers(0, vertex_buffers, offsets);
        if (mesh.index_buffer() && mesh.index_count()) {
            vk::Buffer index_buffer = mesh.index_buffer().buffer;
            _command_buffer.bindIndexBuffer(index_buffer, 0, mesh.index_type());
            _command_buffer.drawIndexed(
                mesh.index_count().value(), mesh.instance_count().value(), 0, 0,
                0
            );
        } else {
            _command_buffer.draw(
                mesh.vertex_count().value(), mesh.instance_count().value(), 0, 0
            );
        }
        end_pipeline();
    }
    void end(backend::Queue& queue) {
        _command_buffer.end();
        rendering = false;
        queue.submit(
            vk::SubmitInfo().setCommandBuffers(_command_buffer), _fence
        );
    }

    Batch(PipelineBase& pipeline, backend::CommandPool& command_pool)
        : Batch(
              pipeline.device,
              command_pool,
              pipeline.render_pass,
              pipeline.pipeline,
              pipeline.pipeline_layout
          ) {}

    void destroy() {
        _device.waitForFences(_fence, true, UINT64_MAX);
        if (_framebuffer) {
            _device.destroyFramebuffer(_framebuffer);
        }
        _mesh.destroy();
        _device.destroyFence(_fence);
        _device.freeCommandBuffers(_command_pool, _command_buffer);
    }

   private:
    backend::Device _device;
    backend::CommandPool _command_pool;

    backend::RenderPass _render_pass;
    backend::Pipeline _pipeline;
    backend::PipelineLayout _pipeline_layout;

    std::vector<backend::DescriptorSet> _descriptor_sets;

    backend::CommandBuffer _command_buffer;
    backend::Fence _fence;

    backend::Framebuffer _framebuffer;
    vk::Extent2D _extent;

    dedicated_mesh_t _mesh;

    bool rendering = false;

    Batch(
        backend::Device& device,
        backend::CommandPool& command_pool,
        backend::RenderPass& render_pass,
        backend::Pipeline& pipeline,
        backend::PipelineLayout& pipeline_layout
    )
        : _mesh(device) {
        this->_device          = device;
        this->_command_pool    = command_pool;
        this->_render_pass     = render_pass;
        this->_pipeline        = pipeline;
        this->_pipeline_layout = pipeline_layout;
        _command_buffer        = _device.allocateCommandBuffers(
            vk::CommandBufferAllocateInfo()
                .setCommandPool(_command_pool)
                .setCommandBufferCount(1)
                .setLevel(vk::CommandBufferLevel::ePrimary)
        )[0];
        _fence = _device.createFence(
            vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
        );
    }

    void begin_pipeline() {
        vk::RenderPassBeginInfo render_pass_info;
        render_pass_info.setRenderPass(_render_pass);
        render_pass_info.setFramebuffer(_framebuffer);
        render_pass_info.setRenderArea(vk::Rect2D().setExtent(_extent));
        _command_buffer.beginRenderPass(
            render_pass_info, vk::SubpassContents::eInline
        );
        _command_buffer.bindPipeline(
            vk::PipelineBindPoint::eGraphics, _pipeline
        );
        _command_buffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics, _pipeline_layout, 0,
            _descriptor_sets, {}
        );
        _command_buffer.setViewport(
            0, vk::Viewport()
                   .setWidth(_extent.width)
                   .setHeight(_extent.height)
                   .setMaxDepth(1.0f)
        );
        _command_buffer.setScissor(0, vk::Rect2D().setExtent(_extent));
    }
    void end_pipeline() { _command_buffer.endRenderPass(); }
};
}  // namespace epix::render::vulkan2