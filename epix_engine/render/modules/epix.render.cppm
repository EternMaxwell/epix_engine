/**
 * @file epix.render.cppm
 * @brief Render module for graphics rendering
 */

export module epix.render;

// Standard library
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Third-party
#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

// Module imports
#include <epix/core.hpp>
#include <epix/assets.hpp>
#include <epix/window.hpp>
#include <epix/transform.hpp>

export namespace epix::render {
    // Forward declarations
    struct RenderContext;
    struct RenderGraph;
    struct RenderNode;
    struct RenderSlot;
    struct Pipeline;
    struct Shader;
    struct Material;
    struct Mesh;
    
    // Camera component
    struct Camera {
        float fov = 60.0f;
        float near_plane = 0.1f;
        float far_plane = 1000.0f;
        
        Camera() = default;
        Camera(float fov, float near_z, float far_z) 
            : fov(fov), near_plane(near_z), far_plane(far_z) {}
        
        glm::mat4 projection_matrix(float aspect_ratio) const;
    };
    
    // Camera 2D
    struct Camera2D {
        float scale = 1.0f;
        
        Camera2D() = default;
        explicit Camera2D(float s) : scale(s) {}
        
        glm::mat4 projection_matrix(float width, float height) const;
    };
    
    // View uniform for shaders
    struct ViewUniform {
        glm::mat4 view;
        glm::mat4 projection;
        glm::mat4 view_projection;
        glm::vec3 camera_position;
    };
    
    // Render phase labels
    struct Transparent {};
    struct Opaque {};
    struct Shadow {};
    struct PostProcess {};
    
    // Extracted view
    struct ExtractedView {
        ViewUniform uniform;
        epix::Entity entity;
    };
    
    // Render context
    struct RenderContext {
        nvrhi::DeviceHandle device;
        nvrhi::CommandListHandle command_list;
        
        RenderContext() = default;
        RenderContext(nvrhi::DeviceHandle dev) : device(dev) {}
    };
    
    // Render graph node
    struct RenderNode {
        std::string name;
        std::function<void(RenderContext&)> execute;
        std::vector<std::string> inputs;
        std::vector<std::string> outputs;
    };
    
    // Render graph
    struct RenderGraph {
        std::vector<RenderNode> nodes;
        std::unordered_map<std::string, nvrhi::TextureHandle> resources;
        
        void add_node(RenderNode node);
        void execute(RenderContext& context);
        void clear();
    };
    
    // Shader asset
    struct Shader {
        nvrhi::ShaderHandle vertex;
        nvrhi::ShaderHandle fragment;
        nvrhi::ShaderHandle compute;
        
        Shader() = default;
        
        static Shader from_spirv(
            std::span<const uint32_t> vertex_spirv,
            std::span<const uint32_t> fragment_spirv
        );
    };
    
    // Material
    struct Material {
        epix::assets::Handle<Shader> shader;
        std::unordered_map<std::string, nvrhi::BindingSetHandle> bindings;
        
        Material() = default;
        Material(epix::assets::Handle<Shader> shd) : shader(shd) {}
    };
    
    // Mesh vertex
    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
        glm::vec4 color;
    };
    
    // Mesh asset
    struct Mesh {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        nvrhi::BufferHandle vertex_buffer;
        nvrhi::BufferHandle index_buffer;
        
        Mesh() = default;
        
        void upload_to_gpu(nvrhi::DeviceHandle device);
    };
    
    // Mesh component
    struct MeshComponent {
        epix::assets::Handle<Mesh> mesh;
        epix::assets::Handle<Material> material;
        
        MeshComponent() = default;
        MeshComponent(
            epix::assets::Handle<Mesh> m,
            epix::assets::Handle<Material> mat
        ) : mesh(m), material(mat) {}
    };
    
    // Pipeline descriptor
    struct PipelineDescriptor {
        nvrhi::GraphicsPipelineDesc desc;
        
        PipelineDescriptor() = default;
    };
    
    // Pipeline cache
    struct PipelineCache {
        std::unordered_map<size_t, nvrhi::GraphicsPipelineHandle> pipelines;
        
        nvrhi::GraphicsPipelineHandle get_or_create(
            nvrhi::DeviceHandle device,
            const PipelineDescriptor& desc
        );
    };
    
    // Render schedule labels
    struct ExtractSchedule {};
    struct RenderSchedule {};
    
    // Window render target
    struct WindowRenderTarget {
        nvrhi::TextureHandle color_texture;
        nvrhi::TextureHandle depth_texture;
        nvrhi::FramebufferHandle framebuffer;
        
        void resize(nvrhi::DeviceHandle device, uint32_t width, uint32_t height);
    };
    
    // Vulkan backend info
    struct VulkanBackend {
        void* instance; // VkInstance
        void* physical_device; // VkPhysicalDevice
        void* device; // VkDevice
        void* queue; // VkQueue
        
        VulkanBackend() = default;
    };
    
    // Render plugin
    struct RenderPlugin {
        void build(epix::App& app);
    };
    
    // Core 2D render graph
    struct Core2DPlugin {
        void build(epix::App& app);
    };
    
    // Extract functions
    void extract_cameras(/* params */);
    void extract_meshes(/* params */);
    void extract_sprites(/* params */);
    
    // Render systems
    void prepare_windows(/* params */);
    void render_2d(/* params */);
    void render_3d(/* params */);
    void present_frames(/* params */);
}  // namespace epix::render
