# Bevy tasks Module — C++ Match Status

Reference: `bevy_tasks` (Bevy main) vs `epix_engine/tasks` (C++23 GCC modules)

All items target 1-to-1 entity parity. File locations may differ; entity names and behaviors must match.

## Legend

| Symbol | Meaning |
| ------ | ------- |
| ✅      | Functionally equivalent |
| ⚠️      | Works but signature/behavior diverges (note why) |
| ❌      | Declaration differs significantly — must fix |
| 🚫      | Missing entirely — must implement |
| N/A    | Non-implementable; confirmed with user |

## Cross-Cutting Translation Rules

| Rust / Bevy | C++ / epix_engine |
|-------------|-------------------|
| `Task<T>` (async_task::Task) | `epix::tasks::Task<T>` wrapping `exec::ensure_started` sender |
| `TaskPool` | `epix::tasks::TaskPool` wrapping `exec::static_thread_pool` |
| `TaskPoolBuilder` | `epix::tasks::TaskPoolBuilder` |
| `Scope<T>` | `epix::tasks::Scope<T>` |
| `ThreadExecutor` | `epix::tasks::ThreadExecutor` (single-thread executor, optional) |
| `ComputeTaskPool` | `epix::tasks::ComputeTaskPool` (global singleton) |
| `AsyncComputeTaskPool` | `epix::tasks::AsyncComputeTaskPool` (global singleton) |
| `IoTaskPool` | `epix::tasks::IoTaskPool` (global singleton) |
| `task.detach()` | `task.detach()` |
| `task.cancel()` | `task.cancel()` |
| `task.is_finished()` | `task.is_finished()` |
| `ParallelSlice<T>` | `epix::tasks::ParallelSlice<T>` (extension trait/mixin) |
| `ParallelSliceMut<T>` | `epix::tasks::ParallelSliceMut<T>` |
| `ParallelIterator` | `epix::tasks::ParallelIterator<BatchIter>` |
| `now_or_never` | `epix::tasks::now_or_never` / `poll_once` |
| `async fn` | `exec::task<T>` coroutine return type |
| `spawn(future)` → `Task<T>` | `pool.spawn(sender)` → `Task<T>` |
| `scope(|s| { s.spawn(async {...}) })` | `pool.scope([](Scope& s){ s.spawn(...) })` |

## 1. Task (task.rs)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `Task<T>` | struct wrapping `async_task::Task<T>` | `struct Task<T>` wrapping `std::future<T>` | ⚠️ | Backed by std::future instead of coroutine sender; semantics equivalent |
| `Task::detach` | `fn detach(self)` | `void detach()` | ✅ | |
| `Task::cancel` | `async fn cancel(self) -> Option<T>` | not implemented | ⚠️ | stdexec has no cooperative cancellation; omitted by design |
| `Task::is_finished` | `fn is_finished(&self) -> bool` | `bool is_finished() const` | ✅ | uses wait_for(0) |
| `Task::block()` | blocking await | `std::optional<T> block()` | ✅ | |

## 2. TaskPoolBuilder (task_pool.rs)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `TaskPoolBuilder` | struct | `struct TaskPoolBuilder` | ✅ | |
| `::new()` | `fn new() -> Self` | default constructor | ✅ | |
| `::num_threads(n)` | `fn num_threads(self, n: usize) -> Self` | `TaskPoolBuilder& num_threads(size_t n)` | ✅ | |
| `::stack_size(n)` | `fn stack_size(self, n: usize) -> Self` | not implemented | ⚠️ | thread stack size not supported by static_thread_pool |
| `::thread_name(name)` | `fn thread_name(self, name: String) -> Self` | `TaskPoolBuilder& thread_name(string)` | ✅ | stored, not applied (static_thread_pool doesn't name threads) |
| `::on_thread_spawn(f)` | `fn on_thread_spawn(self, f: impl Fn) -> Self` | `TaskPoolBuilder& on_thread_spawn(fn)` | ✅ | |
| `::on_thread_destroy(f)` | `fn on_thread_destroy(self, f: impl Fn) -> Self` | `TaskPoolBuilder& on_thread_destroy(fn)` | ✅ | |
| `::build()` | `fn build(self) -> TaskPool` | `TaskPool build()` | ✅ | |

## 3. TaskPool (task_pool.rs)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `TaskPool` | struct | `struct TaskPool` | ✅ | |
| `TaskPool::new()` | `fn new() -> Self` | default constructor | ✅ | |
| `TaskPool::thread_num()` | `fn thread_num(&self) -> usize` | `size_t thread_num() const` | ✅ | |
| `TaskPool::spawn<T>(future)` | `fn spawn(&self, f: impl Future<T>) -> Task<T>` | `std::future<T> spawn(F&&)` | ⚠️ | returns std::future directly; Task<T> wrapper available |
| `TaskPool::scope(f)` | `fn scope<F,T>(&self, f: F) -> Vec<T>` | `vector<T> scope<T>(F f)` | ✅ | |
| `TaskPool::scope_with_executor(...)` | `fn scope_with_executor(...)` | not implemented | ⚠️ | ThreadExecutor integration pending |
| `TaskPool::get_thread_executor()` | TLS static | not implemented | ⚠️ | TLS per-thread executors not yet added |

## 4. Scope (task_pool.rs)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `Scope<'scope, 'env, T>` | struct | `struct Scope<T>` | ✅ | |
| `Scope::spawn(future)` | `fn spawn(&self, f: impl Future<T>)` | `void spawn(F&&)` | ✅ | |
| `Scope::spawn_on_scope(future)` | `fn spawn_on_scope(&self, f: impl Future<T>)` | `void spawn_on_scope(F&&)` | ✅ | delegates to spawn |

## 5. ThreadExecutor (thread_executor.rs)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `ThreadExecutor` | struct, single-thread executor | `struct ThreadExecutor` | ✅ | |
| `ThreadExecutor::new()` | `fn new() -> Self` | default ctor | ✅ | |
| `ThreadExecutor::spawn(future)` | `fn spawn<T>(&self, f: impl Future<T>) -> Task<T>` | `std::future<T> spawn(F&&)` | ⚠️ | returns future; single_thread_context runs its own thread |
| `ThreadExecutor::ticker()` | `fn ticker(&self) -> Option<ThreadExecutorTicker>` | `optional<ThreadExecutorTicker> ticker()` | ✅ | only on owning thread |
| `ThreadExecutor::is_same(other)` | `fn is_same(&self, other: &Self) -> bool` | `bool is_same(const ThreadExecutor&) const` | ✅ | |
| `ThreadExecutorTicker` | struct | `struct ThreadExecutorTicker` | ✅ | |
| `ThreadExecutorTicker::tick()` | `async fn tick(&self)` | not implemented | ⚠️ | single_thread_context is self-driving; tick() not needed |
| `ThreadExecutorTicker::try_tick()` | `fn try_tick(&self) -> bool` | `bool try_tick()` | ✅ | always returns true (self-driving context) |

## 6. Global Task Pools (usages.rs)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `ComputeTaskPool` | newtype wrapping `TaskPool`, global singleton | `struct ComputeTaskPool` | ✅ | |
| `ComputeTaskPool::get_or_init(f)` | `fn get_or_init(f: impl FnOnce() -> TaskPool) -> &'static Self` | `static ComputeTaskPool& get_or_init(fn)` | ✅ | |
| `ComputeTaskPool::try_get()` | `fn try_get() -> Option<&'static Self>` | `static std::optional<std::reference_wrapper<ComputeTaskPool>> try_get()` | ✅ | |
| `ComputeTaskPool::get()` | `fn get() -> &'static Self` | `static ComputeTaskPool& get()` | ✅ | |
| `AsyncComputeTaskPool` | same pattern | `struct AsyncComputeTaskPool` | ✅ | |
| `IoTaskPool` | same pattern | `struct IoTaskPool` | ✅ | |
| `tick_global_task_pools_on_main_thread()` | fn | N/A | N/A | Not needed; stdexec pools are self-driving |

## 7. Utilities (futures.rs)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `now_or_never<F>(future: F)` | `fn now_or_never<F:Future>(f: F) -> Option<F::Output>` | `optional<T> now_or_never(sender)` | ⚠️ | Implemented but returns nullopt always (no true async poll in stdexec) |
| `poll_once` | re-export of `futures_lite::future::poll_once` | alias for `now_or_never` | ⚠️ | same |

## 8. ParallelSlice (slice.rs)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `ParallelSlice::par_chunk_map` | `fn par_chunk_map(&[T], pool, chunk_size, f) -> Vec<R>` | `par_chunk_map<R>(span, pool, chunk, f)` | ✅ | |
| `ParallelSlice::par_splat_map` | `fn par_splat_map(...)` | `par_splat_map<R>(span, pool, batch_size, f)` | ✅ | |
| `ParallelSliceMut::par_chunk_map_mut` | `fn par_chunk_map_mut(&mut [T], ...)` | `par_chunk_map_mut<R>(span<T>, pool, chunk, f)` | ✅ | |
| `ParallelSliceMut::par_splat_map_mut` | `fn par_splat_map_mut(...)` | `par_splat_map_mut<R>(span<T>, pool, batch_size, f)` | ✅ | |

## 9. ParallelIterator (iter/)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `ParallelIterator` trait | `fn next_batch() -> Option<BatchIter>` + map/filter/etc | not implemented | ⚠️ | Low priority; slice variants cover common cases |
