#include <epix/render.hpp>

namespace epix::render {

// One counter per id type, owned by the engine so that every image counting
// through these types shares the same sequence.
std::atomic<std::uint64_t> RenderPipelineId::counter{1};
std::atomic<std::uint64_t> ComputePipelineId::counter{1};

}  // namespace epix::render
