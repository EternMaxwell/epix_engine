#include <epix/task.hpp>

namespace epix::task {

std::optional<asio::any_io_executor> TaskPool::try_get_asio_executor() const {
    if (m_backend_kind != TaskPoolBackend::AsioThreadPool || !m_backend) return std::nullopt;
    return std::static_pointer_cast<internal::AsioThreadPoolBackend>(m_backend)->asio_executor();
}

asio::any_io_executor TaskPool::get_asio_executor() const {
    auto ex = try_get_asio_executor();
    if (!ex) throw std::logic_error("TaskPool backend does not provide an Asio executor");
    return *ex;
}

TaskPool::~TaskPool() = default;

void TaskPool::enqueue_on(std::weak_ptr<void> backend, TaskPoolBackend kind, std::function<void()> fn) {
    auto locked = backend.lock();
    if (!locked) return;
    if (kind == TaskPoolBackend::AsioThreadPool) {
        std::static_pointer_cast<internal::AsioThreadPoolBackend>(std::move(locked))->enqueue(std::move(fn));
    } else {
        std::static_pointer_cast<internal::StaticThreadPoolBackend>(std::move(locked))->enqueue(std::move(fn));
    }
}

TaskPool TaskPool::make_static_thread_pool(size_t n) {
    auto backend = std::make_shared<internal::StaticThreadPoolBackend>(n);
    return {std::move(backend), TaskPoolBackend::StaticThreadPool, n};
}

TaskPool TaskPool::make_asio_thread_pool(size_t n) {
    auto backend = std::make_shared<internal::AsioThreadPoolBackend>(n);
    return {std::move(backend), TaskPoolBackend::AsioThreadPool, n};
}

TaskPool TaskPool::from_builder(const TaskPoolBuilder& b) {
    size_t n    = b.m_num_threads.value_or(available_parallelism());
    auto chosen = b.m_backend.value_or(TaskPoolBackend::StaticThreadPool);

    if (chosen == TaskPoolBackend::AsioThreadPool)
        return make_asio_thread_pool(n);
    else
        return make_static_thread_pool(n);
}

TaskPool TaskPoolBuilder::build() { return TaskPool::from_builder(*this); }

TaskPool::TaskPool() : TaskPool(make_static_thread_pool(available_parallelism())) {}

TaskPool::TaskPool(TaskPoolBuilder builder) : TaskPool(from_builder(builder)) {}

}  // namespace epix::task
