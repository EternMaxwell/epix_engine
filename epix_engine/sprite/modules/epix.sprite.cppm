/**
 * @file epix.sprite.cppm
 * @brief Sprite module for 2D sprite rendering
 */

export module epix.sprite;

#include <glm/glm.hpp>

// Module imports
#include <epix/core.hpp>
#include <epix/image.hpp>

export namespace epix::sprite {
    // Sprite component
    struct Sprite {
        epix::image::Handle<epix::image::Image> image;
        glm::vec4 color = glm::vec4(1.0f);
        glm::vec2 custom_size = glm::vec2(0.0f); // 0 means use image size
        glm::vec2 anchor = glm::vec2(0.5f); // Center anchor
        
        Sprite() = default;
        Sprite(epix::image::Handle<epix::image::Image> img) : image(img) {}
    };
    
    // Texture atlas
    struct TextureAtlas {
        epix::image::Handle<epix::image::Image> texture;
        glm::vec2 size;
        
        TextureAtlas() = default;
        TextureAtlas(epix::image::Handle<epix::image::Image> tex, glm::vec2 sz) 
            : texture(tex), size(sz) {}
    };
    
    // Sprite from atlas
    struct TextureAtlasSprite {
        size_t index = 0;
        glm::vec4 color = glm::vec4(1.0f);
        glm::vec2 custom_size = glm::vec2(0.0f);
        glm::vec2 anchor = glm::vec2(0.5f);
    };
    
    // Sprite plugin
    struct SpritePlugin {
        void build(epix::App& app);
    };
}  // namespace epix::sprite
