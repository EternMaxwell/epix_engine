/**
 * @file epix.render.cppm
 * @brief Render module for graphics rendering via Vulkan/NVRHI
 */

export module epix.render;

#include <epix/core.hpp>
#include <epix/assets.hpp>
#include <epix/window.hpp>
#include <epix/transform.hpp>
#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

export namespace epix::render {
    // Forward declarations
    struct RenderContext;
    struct RenderGraph;
    struct RenderNode;
    struct RenderSlot;
    struct Shader;
    struct Material;
    struct Pipeline;
    
    // Render pipeline ID
    EPIX_MAKE_U64_WRAPPER(RenderPipelineId)
    
    // Camera components
    struct Camera {
        float fov = 60.0f;
        float near = 0.1f;
        float far = 1000.0f;
        
        glm::mat4 projection_matrix(float aspect) const;
    };
    
    struct Camera2D {
        float scale = 1.0f;
        
        glm::mat4 projection_matrix(float width, float height) const;
    };
    
    // View data
    struct ViewUniform {
        glm::mat4 view;
        glm::mat4 projection;
        glm::mat4 view_projection;
        glm::vec3 camera_position;
    };
    
    struct ExtractedView {
        ViewUniform uniform;
        epix::Entity entity;
    };
    
    // Render graph
    namespace graph {
        struct RenderContext {
            nvrhi::DeviceHandle device;
            nvrhi::CommandListHandle command_list;
        };
        
        struct RenderSlot {
            std::string name;
        };
        
        struct RenderNode {
            std::string name;
            std::function<void(RenderContext&)> run;
            std::vector<std::string> inputs;
            std::vector<std::string> outputs;
        };
        
        struct RenderGraph {
            std::vector<RenderNode> nodes;
            std::unordered_map<std::string, nvrhi::TextureHandle> resources;
            
            void add_node(RenderNode node);
            void execute(RenderContext& context);
            void clear();
        };
    }  // namespace graph
    
    using RenderContext = graph::RenderContext;
    using RenderGraph = graph::RenderGraph;
    using RenderNode = graph::RenderNode;
    using RenderSlot = graph::RenderSlot;
    
    // Shader asset
    struct Shader {
        nvrhi::ShaderHandle vertex;
        nvrhi::ShaderHandle fragment;
        nvrhi::ShaderHandle compute;
        
        static epix::assets::Handle<Shader> from_spirv(/* params */);
    };
    
    // Material
    struct Material {
        epix::assets::Handle<Shader> shader;
        std::unordered_map<std::string, nvrhi::BindingSetHandle> bindings;
    };
    
    // Mesh vertex
    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
        glm::vec4 color;
    };
    
    // Mesh
    struct Mesh {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        nvrhi::BufferHandle vertex_buffer;
        nvrhi::BufferHandle index_buffer;
        
        void upload_to_gpu(nvrhi::DeviceHandle device);
    };
    
    // Pipeline
    struct Pipeline {
        nvrhi::GraphicsPipelineHandle handle;
        nvrhi::GraphicsPipelineDesc desc;
    };
    
    struct PipelineCache {
        std::unordered_map<size_t, nvrhi::GraphicsPipelineHandle> cache;
        
        nvrhi::GraphicsPipelineHandle get_or_create(/* params */);
    };
    
    // Vulkan backend
    struct VulkanBackend {
        void* instance;
        void* physical_device;
        void* device;
        void* queue;
    };
    
    // Window render target
    struct WindowRenderTarget {
        nvrhi::TextureHandle color_texture;
        nvrhi::TextureHandle depth_texture;
        nvrhi::FramebufferHandle framebuffer;
        
        void resize(nvrhi::DeviceHandle device, uint32_t width, uint32_t height);
    };
    
    // Render phases
    namespace render_phase {
        struct PhaseItem;
        struct DrawContext;
        struct Transparent {};
        struct Opaque {};
        struct Shadow {};
    }  // namespace render_phase
    
    // Schedule labels
    struct ExtractSchedule {};
    struct RenderSchedule {};
    
    // Extract functions
    void extract_cameras(/* params */);
    void extract_meshes(/* params */);
    
    // Render systems
    void prepare_windows(/* params */);
    void present_frames(/* params */);
    
    // Render plugin
    struct RenderPlugin {
        void build(epix::App& app);
    };
    
}  // namespace epix::render

// Core 2D graph
export namespace epix::core_graph {
    struct Core2D {
        void build(epix::App& app);
    };
}  // namespace epix::core_graph
