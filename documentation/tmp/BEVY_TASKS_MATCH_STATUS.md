# Bevy tasks Module — C++ Match Status

Reference: `bevy_tasks` (Bevy main) vs `epix_engine/tasks` (C++23 GCC modules, asio backend)

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
| `async_task::Task<T>` | `epix::tasks::Task<T>` wrapping `shared_ptr<TaskState<T>>` |
| `Arc<Executor<'static>>` | `asio::any_io_executor` + `shared_ptr<void>` backend |
| `TaskPool` | `epix::tasks::TaskPool` wrapping asio thread_pool or io_context |
| `TaskPoolBuilder` | `epix::tasks::TaskPoolBuilder` |
| `Scope<'scope,'env,T>` | `epix::tasks::Scope<T>` |
| `ThreadExecutor` | `epix::tasks::ThreadExecutor` wrapping `asio::thread_pool{1}` |
| `OnceLock<$type>` | `static Name*` + `std::once_flag` |
| `Deref to TaskPool` | `pool()` accessor + delegated `spawn()`/`scope()`/`thread_num()` |
| `spawn(future)` → `Task<T>` | `spawn(callable)` → `Task<T>` via `asio::post` |
| `scope(|s| s.spawn(async {...}))` | `scope([](Scope& s){ s.spawn(callable) })` |
| `thread::Builder::new().name(n)` | `BS::this_thread::set_os_thread_name(n)` (cross-platform) |

## 1. Task (task.rs → task.cppm)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `Task<T>` | struct wrapping `async_task::Task<T>` | `struct Task<T>` wrapping `shared_ptr<TaskState<T>>` | ⚠️ | Different backing: shared state + mutex/cv vs async_task; same semantics |
| `Task::new(task)` | `fn new(task: async_task::Task<T>) -> Self` | `static make() -> pair<Task, shared_ptr<State>>` | ⚠️ | Factory instead of ctor; internal detail |
| `Task::detach` | `fn detach(self)` | `void detach()` | ✅ | |
| `Task::cancel` | `async fn cancel(self) -> Option<T>` | `optional<T> cancel()` / `void cancel()` | ⚠️ | Synchronous: sets cancelled flag, waits for completion, returns value; not async cooperative |
| `Task::is_finished` | `fn is_finished(&self) -> bool` | `bool is_finished() const` | ✅ | |
| `Task as Future` | `impl Future for Task<T>` | `operator()(CompletionToken)` | ⚠️ | Asio completion token model instead of Future trait; usable with co_await |
| `Task::block()` | N/A (bevy uses `.await`) | `std::optional<T> block()` | ⚠️ | C++ addition for synchronous blocking |

## 2. TaskPoolBuilder (task_pool.rs → task_pool.cppm)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `TaskPoolBuilder` | `#[derive(Default)] struct` | `struct TaskPoolBuilder` | ✅ | Fields: `m_num_threads`, `m_stack_size`, `m_thread_name`, `m_on_thread_spawn`, `m_on_thread_destroy`, `m_backend` |
| `::new()` | `fn new() -> Self` (Default) | default constructor | ✅ | |
| `::num_threads(n)` | `fn num_threads(self, n: usize) -> Self` | `TaskPoolBuilder& num_threads(size_t n)` | ✅ | Builder returns `&` instead of by-value (C++ idiom) |
| `::stack_size(n)` | `fn stack_size(self, n: usize) -> Self` | `TaskPoolBuilder& stack_size(size_t n)` | ⚠️ | Field stored but not applied — `std::thread` has no stack_size API |
| `::thread_name(name)` | `fn thread_name(self, name: String) -> Self` | `TaskPoolBuilder& thread_name(string)` | ✅ | Applied via `BS::this_thread::set_os_thread_name` in IoContext backend |
| `::on_thread_spawn(f)` | `fn on_thread_spawn(self, f: impl Fn()) -> Self` | `TaskPoolBuilder& on_thread_spawn(function<void()>)` | ✅ | |
| `::on_thread_destroy(f)` | `fn on_thread_destroy(self, f: impl Fn()) -> Self` | `TaskPoolBuilder& on_thread_destroy(function<void()>)` | ✅ | |
| `::build()` | `fn build(self) -> TaskPool` | `TaskPool build()` | ✅ | Delegates to `TaskPool::from_builder` |

## 3. TaskPool (task_pool.rs → task_pool.cppm)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `TaskPool` struct fields | `executor: Arc<Executor>`, `threads: Vec<JoinHandle>`, `shutdown_tx` | `m_backend: shared_ptr<void>`, `m_executor: any_io_executor`, `m_thread_count` | ⚠️ | Different async runtime; same ownership/lifetime semantics |
| `TaskPool::new()` | `fn new() -> Self` (Default) | default constructor | ✅ | Uses `available_parallelism()` (matches bevy) |
| `TaskPool::new_internal(builder)` | private, creates threads with `thread::Builder` | `from_builder(builder)` / `make_io_context` / `make_thread_pool` | ✅ | Thread naming: `"{name} ({i})"` matches bevy; `CallOnDrop` RAII guard ensures `on_thread_destroy` runs reliably |
| `TaskPool::thread_num()` | `fn thread_num(&self) -> usize` | `size_t thread_num() const` | ✅ | |
| `TaskPool::spawn(future)` | `fn spawn(&self, f: impl Future<T>) -> Task<T>` | `Task<T> spawn(F&& callable)` | ⚠️ | Takes callable not future; returns Task<T> |
| `TaskPool::spawn_local(future)` | `fn spawn_local(&self, f: impl Future<T>) -> Task<T>` | not implemented | 🚫 | Needs thread_local LOCAL_EXECUTOR |
| `TaskPool::with_local_executor(f)` | `fn with_local_executor<F,R>(&self, f: F) -> R` | not implemented | 🚫 | Needs thread_local LOCAL_EXECUTOR |
| `TaskPool::get_thread_executor()` | TLS `Arc<ThreadExecutor<'static>>` | not implemented | 🚫 | Needs thread_local THREAD_EXECUTOR |
| `TaskPool::scope(f)` | `fn scope<F,T>(&self, f: F) -> Vec<T>` | `vector<T> scope<T>(F f)` | ✅ | |
| `TaskPool::scope_with_executor(...)` | `fn scope_with_executor(tick, ext_exec, f) -> Vec<T>` | not implemented | 🚫 | Depends on ThreadExecutor ticking model |
| `Drop for TaskPool` | `shutdown_tx.close()` + join threads | `~ThreadPoolBundle()` calls `wait()`; `~IoBundle()` resets guard + joins | ✅ | Different mechanism, same drain-then-join semantics |
| `LOCAL_EXECUTOR` | `thread_local! { LocalExecutor }` | not implemented | 🚫 | |
| `THREAD_EXECUTOR` | `thread_local! { Arc<ThreadExecutor> }` | not implemented | 🚫 | |

## 4. Scope (task_pool.rs → task_pool.cppm)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `Scope` struct fields | `executor`, `external_executor`, `scope_executor`, `spawned: ConcurrentQueue` | `m_executor: any_io_executor`, `m_tasks: vector<Task<T>>` | ⚠️ | No external/scope executor distinction; no ConcurrentQueue (vector suffices for sync spawn) |
| `Scope::spawn(future)` | `fn spawn(&self, f: Fut)` | `void spawn(F&& callable)` | ⚠️ | Takes callable instead of future |
| `Scope::spawn_on_scope(future)` | `fn spawn_on_scope(&self, f: Fut)` | `void spawn_on_scope(F&&)` | ✅ | Delegates to spawn (no separate scope executor) |
| `Scope::spawn_on_external(future)` | `fn spawn_on_external(&self, f: Fut)` | `void spawn_on_external(F&&)` | ⚠️ | Delegates to `spawn()` (no separate external executor) |
| `Drop for Scope` | cancels all remaining tasks | tasks destroyed naturally | ⚠️ | Tasks already complete before Scope drops (collect_results called first) |

## 5. ThreadExecutor (thread_executor.rs → thread_executor.cppm)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `ThreadExecutor` fields | `executor: Executor`, `thread_id: ThreadId` | `m_ctx: asio::thread_pool{1}`, `m_thread_id: thread::id` | ⚠️ | asio single-thread pool is self-driving; bevy's needs manual ticking |
| `ThreadExecutor::new()` | `fn new() -> Self` (Default) | default ctor | ✅ | |
| `ThreadExecutor::spawn(future)` | `fn spawn<T>(&self, f: impl Future<T>) -> Task<T>` | `Task<T> spawn(F&& callable)` | ⚠️ | Takes callable; asio::co_spawn internally |
| `ThreadExecutor::ticker()` | `fn ticker(&self) -> Option<ThreadExecutorTicker>` | `optional<ThreadExecutorTicker> ticker()` | ✅ | Only on owning thread |
| `ThreadExecutor::is_same(other)` | `fn is_same(&self, other: &Self) -> bool` | `bool is_same(const ThreadExecutor&) const` | ✅ | Pointer equality |
| `ThreadExecutorTicker` fields | `executor: &ThreadExecutor`, `_marker: PhantomData<*const ()>` | `m_executor: ThreadExecutor*` | ✅ | |
| `ThreadExecutorTicker::tick()` | `async fn tick(&self)` | not implemented | 🚫 | asio thread_pool is self-driving; tick is meaningless |
| `ThreadExecutorTicker::try_tick()` | `fn try_tick(&self) -> bool` | `bool try_tick()` returns false | ⚠️ | No-op; asio context self-drives |

## 6. Global Task Pools (usages.rs → usages.cppm)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `ComputeTaskPool` | newtype `ComputeTaskPool(TaskPool)` with `Deref` | `struct ComputeTaskPool` with `pool()` accessor | ⚠️ | C++ has no Deref; delegates spawn/scope/thread_num |
| `::get_or_init(f)` | `fn get_or_init(f: impl FnOnce() -> TaskPool) -> &Self` | `static Name& get_or_init(function<TaskPool()>)` | ✅ | `once_flag` + heap allocation |
| `::try_get()` | `fn try_get() -> Option<&Self>` | `static Name* try_get()` | ✅ | Raw pointer = nullable ref in C++ |
| `::get()` | `fn get() -> &Self` (panics) | `static Name& get()` (terminates) | ✅ | |
| `AsyncComputeTaskPool` | same macro pattern | same macro pattern | ✅ | |
| `IoTaskPool` | same macro pattern | same macro pattern | ✅ | |
| `tick_global_task_pools_on_main_thread()` | calls `with_local_executor` + `try_tick` x100 | `void tick_global_task_pools_on_main_thread()` | ⚠️ | No-op; asio pools are self-driving, no ticking needed |

## 7. Utilities (futures.rs → futures.cppm)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `now_or_never(future)` | `fn now_or_never<F:Future>(f: F) -> Option<F::Output>` | `optional<T> now_or_never(Task<T>&)` | ✅ | Checks `is_finished()` then blocks |
| `check_ready(future)` | `fn check_ready<F:Future+Unpin>(f: &mut F) -> Option<F::Output>` | `optional<T> check_ready(Task<T>&)` | ✅ | Same as now_or_never |

## 8. ParallelSlice (slice.rs → slice.cppm)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `ParallelSlice::par_chunk_map` | trait method on `&[T]` | `par_chunk_map(span, pool, chunk_size, f)` | ✅ | Free function instead of trait method |
| `ParallelSlice::par_splat_map` | trait method on `&[T]` | `par_splat_map(span, pool, max_tasks, f)` | ✅ | |
| `ParallelSliceMut::par_chunk_map_mut` | trait method on `&mut [T]` | `par_chunk_map_mut(span, pool, chunk_size, f)` | ✅ | |
| `ParallelSliceMut::par_splat_map_mut` | trait method on `&mut [T]` | `par_splat_map_mut(span, pool, max_tasks, f)` | ✅ | |

## 9. ParallelIterator (iter/)

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `ParallelIterator` trait | `next_batch()` + `count`, `last`, `nth`, `chain`, `map`, `filter`, etc. | not implemented | 🚫 | Low priority; slice functions cover common cases |

## 10. lib.rs exports

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `available_parallelism()` | `fn available_parallelism() -> usize` | `size_t available_parallelism()` | ✅ | Wraps `hardware_concurrency()` with fallback to 1 |
| `block_on(future)` | `fn block_on<T>(future: impl Future<T>) -> T` | not implemented | 🚫 | Bevy uses `pollster::block_on` |
| `ConditionalSend` | `trait ConditionalSend: Send` | N/A | N/A | C++ has no Send/Sync trait system |
| `BoxedFuture` | `type BoxedFuture<'a, T>` | N/A | N/A | C++ type erasure differs fundamentally |
