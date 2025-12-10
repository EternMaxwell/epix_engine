// Module partition for forward declarations
// Provides forward declarations for all types in epix.core

export module epix.core:fwd;

export namespace epix::core {
    // Core types
    struct Tick;
    struct ComponentTicks;
    struct TickRefs;
    
    struct Entity;
    struct Entities;
    
    struct World;
    struct WorldCell;
    struct DeferredWorld;
    
    struct EntityRef;
    struct EntityRefMut;
    struct EntityWorldMut;
    
    struct ComponentInfo;
    
    struct App;
    struct AppRunner;
    
    // Type system
    namespace type_system {
        struct TypeId;
        struct TypeInfo;
        struct TypeRegistry;
    }
    
    // Meta
    namespace meta {
        template<typename T>
        struct type_id;
        
        template<typename T>
        struct type_index;
    }
    
    // Query
    namespace query {
        template<typename... T>
        struct Query;
        
        template<typename... T>
        struct Filter;
        
        template<typename T>
        struct With;
        
        template<typename T>
        struct Without;
        
        template<typename T>
        struct Has;
        
        template<typename T>
        struct Item;
        
        template<typename T>
        struct Opt;
        
        template<typename... T>
        struct Or;
        
        struct Added;
        struct Modified;
    }
    
    // System
    namespace system {
        struct Commands;
        struct EntityCommands;
        
        template<typename T>
        struct Local;
        
        template<typename... T>
        struct ParamSet;
        
        struct System;
        struct SystemMeta;
        
        template<typename T>
        concept SystemParam = requires { typename T::Param; };
    }
    
    // Schedule
    namespace schedule {
        struct Schedule;
        struct SystemSetLabel;
        struct ExecuteConfig;
        struct SetConfig;
    }
    
    // Event
    namespace event {
        template<typename T>
        struct Events;
        
        template<typename T>
        struct EventReader;
        
        template<typename T>
        struct EventWriter;
    }
    
    // App
    namespace app {
        struct AppLabel;
        
        template<typename T>
        struct State;
        
        template<typename T>
        struct NextState;
        
        // Schedule labels
        struct Startup;
        struct PreStartup;
        struct PostStartup;
        struct Update;
        struct PreUpdate;
        struct PostUpdate;
        struct First;
        struct Last;
        struct Exit;
        struct PreExit;
        struct PostExit;
        
        template<typename T>
        struct OnEnter;
        
        template<typename T>
        struct OnExit;
        
        template<typename T>
        struct OnChange;
        
        struct StateTransition;
        struct AppExit;
        struct LoopPlugin;
    }
    
    // Hierarchy
    namespace hierarchy {
        struct Parent;
        struct Children;
    }
    
    // Storage
    namespace storage {
        struct Table;
        struct Tables;
        struct ComponentSparseSet;
        struct SparseSets;
        struct Resources;
    }
    
    // Archetype
    struct Archetype;
    struct Archetypes;
    struct ArchetypeId;
    struct ArchetypeRow;
    
    // Bundle
    struct Bundle;
    struct BundleId;
    struct Bundles;
    
    // Table/Column IDs
    struct TableId;
    struct TableRow;
    struct ColumnId;
    
    // Component
    struct Components;
    struct ComponentId;
    
    // Change detection
    template<typename T>
    struct Ref;
    
    template<typename T>
    struct Mut;
    
    template<typename T>
    struct Res;
    
    template<typename T>
    struct ResMut;
    
    struct Ticks;
    struct TicksMut;
    
    // Command queue
    struct CommandQueue;
    
    // Utility
    template<typename T>
    concept FromWorld = requires(T t, World& w) {
        { T::from_world(w) } -> std::same_as<T>;
    };
    
}  // export namespace epix::core
