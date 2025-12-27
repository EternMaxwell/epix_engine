# Schedule Execute Implementation - Technical Details

This document provides technical details about how the schedule execution system works internally, which is similar to Rust Bevy's implementation.

## Execution Overview

The `Schedule::execute()` method implements a parallel execution system with automatic dependency resolution, similar to Bevy's executor. Here's how it works:

### 1. Preparation Phase (`Schedule::prepare()`)

Before execution, the schedule must be prepared:

```cpp
auto result = schedule.prepare(true); // true = check for errors
if (!result.has_value()) {
    // Handle preparation errors (cycles, conflicting dependencies, etc.)
}
```

The preparation phase:
1. **Validates edges**: Ensures all referenced system sets exist
2. **Detects cycles**: Checks for cyclic dependencies and hierarchies
3. **Validates hierarchies**: Ensures parents don't have dependencies between them
4. **Builds execution cache**: Creates optimized data structures for execution

### 2. Initialization Phase

Systems must be initialized before first execution:

```cpp
schedule.initialize_systems(world);
```

This:
- Calls `System::initialize()` for each system
- Collects access information (which resources/components each system uses)
- Stores last execution tick for change detection

### 3. Execution Phase (`Schedule::execute()`)

The execution algorithm is similar to Bevy's multi-threaded executor:

#### Phase 3.1: Setup

```cpp
// Create execution state
ExecutionState exec_state {
    .running_count = 0,           // Currently running systems
    .remaining_count = N,          // Systems not yet finished
    .ready_nodes = {},            // Systems ready to run
    .finished_nodes = {},         // Systems that have finished
    .entered_nodes = {},          // Systems that have entered execution
    .dependencies = {},           // Dependency tracking
    .condition_met_nodes = {},    // Condition tracking
    .wait_count = {},            // Number of unresolved dependencies per system
};
```

#### Phase 3.2: Main Execution Loop

The executor uses a work-stealing parallel execution model:

```cpp
do {
    // 1. Enter ready systems
    enter_ready();  // Dispatches systems that are ready to run
    
    // 2. Wait for finished systems
    auto finishes = exec_state.finished_queue.try_pop();
    if (finishes.empty()) {
        if (exec_state.running_count == 0) {
            break;  // All done!
        }
        finishes = exec_state.finished_queue.pop();  // Block until something finishes
    }
    
    // 3. Process finished systems
    for (auto finished_index : finishes) {
        // Mark as finished
        exec_state.finished_nodes.set(finished_index);
        
        // Notify successors
        for (auto successor : cached_node.successors) {
            exec_state.wait_count[successor]--;
            if (exec_state.wait_count[successor] == 0) {
                exec_state.ready_stack.push_back(successor);  // Now ready!
            }
        }
        
        // Handle hierarchical sets
        for (auto parent : cached_node.parents) {
            exec_state.child_count[parent]--;
            if (exec_state.child_count[parent] == 0) {
                exec_state.finished_queue.push(parent);  // Parent set finished
            }
        }
    }
} while (true);
```

#### Phase 3.3: System Dispatch

When a system is ready, it's dispatched to the thread pool:

```cpp
auto dispatch_system = [&](size_t index) {
    CachedNode& cached_node = cache->nodes[index];
    
    dispatcher.dispatch_system(
        *cached_node.node->system.get(),
        {},  // Input
        cached_node.node->system_access,  // Access requirements
        DispatchConfig {
            .on_finish = [&, index]() { 
                exec_state.finished_queue.push(index); 
            },
            .on_error = [&](const RunSystemError& error) {
                // Log error
            }
        }
    );
    
    exec_state.running_count++;
};
```

### 4. Conditional Execution

Systems with `run_if()` conditions are checked before execution:

```cpp
auto check_cond = [&](size_t index) -> bool {
    // Try to run all untested conditions
    for (auto cond_index : exec_state.untest_conditions[index]) {
        auto res = dispatcher.try_run_system(
            condition,
            {},
            cached_node.node->condition_access[cond_index]
        );
        
        if (res.has_value()) {
            // Condition ran, update result
            bool passed = res->value();
            exec_state.condition_met_nodes.set(index, 
                passed && exec_state.condition_met_nodes.contains(index));
            exec_state.untest_conditions[index].reset(cond_index);
        }
    }
    
    return all_conditions_tested;
};
```

If conditions can't run due to conflicts, the system is placed in `pending_ready` and retried later.

### 5. Hierarchical Execution

System sets can have parent-child relationships:

```cpp
// Child systems run within parent set
ParentSet {
    ChildSystem1,
    ChildSystem2
}

// Parent only finishes when all children finish
if (exec_state.child_count[parent_index] == 0) {
    exec_state.finished_queue.push(parent_index);
}
```

### 6. Deferred Application

Systems can defer operations (like entity spawns) until after execution:

```cpp
ExecuteConfig config;
config.apply_direct = false;  // Don't apply during execution
config.apply_end = true;      // Apply at end

schedule.execute(dispatcher, config);

// Later, manually apply if needed
schedule.apply_deferred(world);
```

## Comparison with Bevy's Implementation

The epix_engine schedule executor is conceptually very similar to Bevy's:

### Similarities

1. **Dependency Graph**: Both build a dependency graph from system ordering constraints
2. **Parallel Execution**: Both use thread pools to run systems in parallel
3. **Access Tracking**: Both track which resources/components each system accesses
4. **Automatic Scheduling**: Both automatically determine parallelization based on access conflicts
5. **Hierarchical Sets**: Both support system sets with parent-child relationships
6. **Conditional Execution**: Both support run_if conditions
7. **Multi-threaded Executor**: Both use similar algorithms for parallel dispatch

### Implementation Details

#### Bevy's Executor

```rust
// Bevy's simplified executor loop
while let Some(system_index) = ready_queue.pop() {
    let system = &systems[system_index];
    
    // Check if system can run (no conflicting access)
    if can_run(system, running_systems) {
        thread_pool.spawn(move || {
            system.run(world);
            mark_finished(system_index);
        });
    }
}
```

#### epix_engine's Executor

```cpp
// epix_engine's executor loop (simplified)
while (true) {
    // Dispatch ready systems
    for (auto index : ready_stack) {
        if (check_conditions(index)) {
            dispatcher.dispatch_system(system, access, config);
        }
    }
    
    // Wait for finished systems
    auto finishes = finished_queue.pop();
    
    // Update dependencies
    for (auto finished : finishes) {
        notify_successors(finished);
    }
    
    if (all_done) break;
}
```

### Key Differences

| Aspect | Bevy | epix_engine |
|--------|------|-------------|
| Language | Rust | C++ |
| Memory Safety | Compile-time via borrow checker | Runtime via access tracking |
| Thread Pool | tokio/rayon | BS::thread_pool |
| Change Detection | Built-in with ComponentTicks | Tick-based via SystemMeta |
| System Parameters | Type-based via SystemParam trait | Template-based via SystemParam |
| Commands | Commands buffer | Commands/DeferredWorld |

## Performance Characteristics

### Time Complexity

- **Preparation**: O(N log N) where N is number of systems
  - Topological sort for dependency resolution
  - Cycle detection via DFS

- **Execution**: O(N) best case (fully parallel), O(N) worst case (fully sequential)
  - Each system runs once
  - Overhead is O(1) per system dispatch

### Space Complexity

- **Cache**: O(N + E) where N = systems, E = edges
- **Execution State**: O(N) for tracking system states
- **Thread Pool**: O(T) where T = thread count

### Parallelism

Maximum parallelism is achieved when:
1. Systems access disjoint sets of components/resources
2. No ordering constraints force sequential execution
3. Thread pool has sufficient threads

Example:
```cpp
// These can all run in parallel
void system_a(Query<Item<Position&>> q) {}      // Writes Position
void system_b(Query<Item<Velocity&>> q) {}      // Writes Velocity
void system_c(Query<Item<Health&>> q) {}        // Writes Health

// Maximum parallelism = min(3, thread_count)
```

## Debugging

### Enable Debug Output

```cpp
// Check for errors during prepare
auto result = schedule.prepare(true);  // true = report errors
if (!result.has_value()) {
    auto& error = result.error();
    switch (error.type) {
        case SchedulePrepareError::Type::CyclicDependency:
            // Handle cycle in dependencies
            break;
        case SchedulePrepareError::Type::CyclicHierarchy:
            // Handle cycle in set hierarchy
            break;
        case SchedulePrepareError::Type::ParentsWithDeps:
            // Handle conflicting parent dependencies
            break;
    }
}
```

### System Naming

Always name your systems for better debugging:

```cpp
schedule.add_systems(
    into(my_system)
        .set_name("my_system")  // Shows in error messages
);
```

### Execution Warnings

If systems don't execute, the executor logs warnings:

```
[schedule] Some systems are not executed, check for cycles in the graph
    Remaining: (system: my_system), (set: MySet#0)
    Not Exited: (system: stuck_system)
```

## Best Practices

1. **Minimize Dependencies**: Fewer dependencies = more parallelism
2. **Use System Sets**: Group related systems for cleaner ordering
3. **Name Everything**: Helps with debugging
4. **Prepare Once**: Call `prepare()` after adding all systems
5. **Check Access**: Systems with same mutable access run sequentially
6. **Light Conditions**: Keep `run_if()` conditions fast
7. **Batch Operations**: Group similar operations in one system
8. **Profile**: Use Tracy or similar tools to find bottlenecks

## Example: Custom Executor Loop

For advanced use cases, you might want custom control:

```cpp
// Initialize
schedule.prepare(true);
schedule.initialize_systems(world);

// Custom execution loop
for (int frame = 0; frame < 1000; frame++) {
    // Update resources
    world.resource_mut<FrameCount>()->count = frame;
    
    // Execute schedule
    schedule.execute(dispatcher);
    dispatcher.wait();
    
    // Apply deferred (if not using apply_direct)
    schedule.apply_deferred(world);
    
    // Check change tick
    schedule.check_change_tick(world.change_tick());
    
    // Custom logic
    if (should_exit()) break;
}
```

## See Also

- [Schedule Execution Guide](schedule_execution.md) - User-facing documentation
- [System Documentation](systems.md) - How to write systems
- [Bevy's Schedule Documentation](https://docs.rs/bevy/latest/bevy/ecs/schedule/index.html) - Original inspiration
