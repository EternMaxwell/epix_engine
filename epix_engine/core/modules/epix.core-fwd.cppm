/**
 * @file epix.core-fwd.cppm
 * @brief Forward declarations partition for epix.core
 */

export module epix.core:fwd;

/**
 * Forward declarations for core types
 */
export namespace epix::core {
    struct Tick;
    
    struct Entity;
    struct Entities;
    
    struct World;
    struct WorldCell;
    
    struct EntityRef;
    struct EntityRefMut;
    struct EntityWorldMut;
    
    struct ComponentInfo;
    
    struct App;
}  // namespace epix::core
