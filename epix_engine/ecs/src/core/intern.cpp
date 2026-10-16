#include <epix/ecs/core/intern.hpp>

#include <mutex>
#include <unordered_map>
#include <utility>

namespace epix::ecs {

InternerBase* interner_slot(const meta::type_index& value_type, InternerBase* (*make)()) {
    // One map for the whole program. This function is defined only in the engine library, so
    // its local static is the single instance every module shares - which is the point: the
    // module that interns a value and the module that looks it up need the same pointer.
    static std::mutex mutex;
    static std::unordered_map<meta::type_index, std::unique_ptr<InternerBase>> interners;
    const std::lock_guard lock(mutex);
    auto& slot = interners[value_type];
    if (!slot) {
        slot.reset(make());
    }
    return slot.get();
}

}  // namespace epix::ecs
