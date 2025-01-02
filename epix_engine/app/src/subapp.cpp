#include "epix/app.h"

using namespace epix::app;

EPIX_API void SubApp::tick_events() {
    for (auto& [ptr, queue] : m_world.m_event_queues) {
        queue->tick();
    }
}

EPIX_API void SubApp::end_commands() {
    for (auto& command : m_command_cache) {
        command.end();
    }
    m_command_cache.clear();
}

EPIX_API void SubApp::update_states() {
    for (auto& update : m_state_updates) {
        update->run(this, this);
    }
}