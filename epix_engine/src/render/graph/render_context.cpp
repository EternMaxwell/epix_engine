#include "epix/render/graph.h"

using namespace epix::render::graph;
using namespace epix::render;

EPIX_API RenderContext::RenderContext(nvrhi::DeviceHandle device) : m_device(device) {}

EPIX_API nvrhi::DeviceHandle RenderContext::device() const { return m_device; }
EPIX_API nvrhi::CommandListHandle RenderContext::commands() {
    if (!m_command_list) {
        m_command_list = m_device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
        m_command_list.value()->open();
    }
    return *m_command_list;
}
EPIX_API nvrhi::CommandListHandle RenderContext::begin_render_pass(const nvrhi::GraphicsState& state) {
    auto cmd_list = commands();
    cmd_list->setGraphicsState(state);
    return cmd_list;
}
EPIX_API void RenderContext::add_command_list(const nvrhi::CommandListHandle& command_list) {
    m_closed_command_lists.push_back(command_list);
}
EPIX_API void RenderContext::flush_encoder() {
    if (m_command_list) {
        m_command_list.value()->close();
        m_closed_command_lists.emplace_back(std::move(*m_command_list));
        m_command_list.reset();
    }
}
EPIX_API std::vector<nvrhi::CommandListHandle> RenderContext::finish() {
    flush_encoder();
    return std::move(m_closed_command_lists);
}