/**
 * @file epix.core-hierarchy.cppm
 * @brief Hierarchy partition for entity parent-child relationships
 */

export module epix.core:hierarchy;

import :fwd;
import :entities;

#include <optional>
#include <vector>

export namespace epix::core::hierarchy {
    // Parent component
    struct Parent {
        Entity parent;
        
        Parent(Entity p) : parent(p) {}
        
        Entity get() const { return parent; }
    };
    
    // Children component
    struct Children {
        std::vector<Entity> children;
        
        Children() = default;
        
        const std::vector<Entity>& get() const { return children; }
        std::vector<Entity>& get_mut() { return children; }
        
        void add(Entity child) {
            children.push_back(child);
        }
        
        void remove(Entity child) {
            children.erase(
                std::remove(children.begin(), children.end(), child),
                children.end()
            );
        }
        
        size_t len() const { return children.size(); }
        bool is_empty() const { return children.empty(); }
    };
}  // namespace epix::core::hierarchy
