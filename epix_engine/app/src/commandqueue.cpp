#include "epix/app/world.h"
#include "epix/app/world_data.h"

using namespace epix::app;

EPIX_API void CommandQueue::flush(World& world) {
    std::unique_lock lock(m_mutex);
    size_t pointer = 0;
    while (pointer < m_commands.size()) {
        auto index         = m_commands[pointer++];
        auto& command_data = m_registry[index];
        auto* pcommand     = reinterpret_cast<void*>(&m_commands[pointer]);
        command_data.apply(world, pcommand);
        command_data.destruct(pcommand);
        pointer += command_data.size;
    }
    m_commands.clear();
}