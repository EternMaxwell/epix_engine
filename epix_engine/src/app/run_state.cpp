#include "epix/app/run_state.h"

using namespace epix::app;

EPIX_API RunState::RunState(
    async::RwLock<World>::WriteGuard&& world, Executors& executors
)
    : world(std::move(world)), executors(&executors) {
    m_apply_commands = IntoSystem::into_system([](World& world) {
        world.command_queue().apply(world);
    });
    m_apply_commands->initialize(*this->world);
}
EPIX_API RunState::~RunState() { wait(); }

EPIX_API void RunState::apply_commands() {
    run_system(
        m_apply_commands.get(), RunState::RunSystemConfig{
                                    .on_finish = []() {},
                                    .on_start  = []() {},
                                    .executor  = ExecutorType::SingleThread
                                }
    );
}
EPIX_API bool RunState::wait() {
    std::unique_lock lock(m_system_mutex);
    m_system_cv.wait(lock, [this]() {
        return running_systems.empty() && waiting_system_callers.empty();
    });
    return true;
}

EPIX_API Executors::Executors() {
    // Default executor pool
    // add_pool(ExecutorLabel(), "default", 4);
    // add_pool(ExecutorLabel(ExecutorType::SingleThread), "single", 1);
};

EPIX_API Executors::executor_t* Executors::get_pool(const ExecutorLabel& label
) noexcept {
    auto write = pools.write();
    auto it    = write->find(label);
    if (it != write->end()) {
        return it->second.get();
    }
    return nullptr;
};
EPIX_API void Executors::add_pool(
    const ExecutorLabel& label, size_t count
) noexcept {
    auto write = pools.write();
    write->emplace(label, std::make_unique<executor_t>(count, [label]() {
                       BS::this_thread::set_os_thread_name(label.name());
                   }));
};
EPIX_API void Executors::add_pool(
    const ExecutorLabel& label, const std::string& name, size_t count
) noexcept {
    auto write = pools.write();
    write->emplace(label, std::make_unique<executor_t>(count, [name]() {
                       BS::this_thread::set_os_thread_name(name);
                   }));
};