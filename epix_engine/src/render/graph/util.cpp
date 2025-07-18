#include "epix/render/graph.h"

using namespace epix::render::graph;
using namespace epix::render;

EPIX_API std::string_view type_name(SlotType type) {
    switch (type) {
        case SlotType::Buffer:
            return "Buffer";
        case SlotType::Texture:
            return "Texture";
        case SlotType::Sampler:
            return "Sampler";
        case SlotType::Entity:
            return "Entity";
    }
    return "Unknown";
}