module;
#include <epix/tasks.hpp>

export module epix.tasks;

export namespace epix::tasks {
using epix::tasks::AsyncComputeTaskPool;
using epix::tasks::check_ready;
using epix::tasks::ComputeTaskPool;
using epix::tasks::IoTaskPool;
using epix::tasks::now_or_never;
using epix::tasks::par_chunk_map;
using epix::tasks::par_chunk_map_mut;
using epix::tasks::par_splat_map;
using epix::tasks::par_splat_map_mut;
using epix::tasks::Scope;
using epix::tasks::Task;
using epix::tasks::TaskPool;
using epix::tasks::TaskPoolBackend;
using epix::tasks::TaskPoolBuilder;
using epix::tasks::ThreadExecutor;
using epix::tasks::ThreadExecutorTicker;
}  // namespace epix::tasks
