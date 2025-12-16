/**
 * @file epix.core.cppm
 * @brief C++20 module interface for epix core module
 * 
 * This module exports the entire epix core ECS framework including:
 * - Application framework
 * - Entity Component System (ECS)
 * - Type system and metadata
 * - Query system
 * - Schedule and system management
 * - Event system
 * - World management
 */

// Global module fragment - for third-party and standard library headers
module;

// Standard library headers
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <expected>
#include <format>
#include <functional>
#include <future>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <ranges>
#include <ratio>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// Third-party library headers
#include <BS_thread_pool.hpp>
#include <spdlog/spdlog.h>

// Include all core headers to make their declarations available
#include "../../core/include/epix/api/macros.hpp"
#include "../../core/include/epix/core/app.hpp"
#include "../../core/include/epix/core/app/app_sche.hpp"
#include "../../core/include/epix/core/app/extract.hpp"
#include "../../core/include/epix/core/app/loop.hpp"
#include "../../core/include/epix/core/app/runner.hpp"
#include "../../core/include/epix/core/app/state.hpp"
#include "../../core/include/epix/core/archetype.hpp"
#include "../../core/include/epix/core/bundle.hpp"
#include "../../core/include/epix/core/bundleimpl.hpp"
#include "../../core/include/epix/core/change_detection.hpp"
#include "../../core/include/epix/core/component.hpp"
#include "../../core/include/epix/core/entities.hpp"
#include "../../core/include/epix/core/event/events.hpp"
#include "../../core/include/epix/core/event/reader.hpp"
#include "../../core/include/epix/core/event/writer.hpp"
#include "../../core/include/epix/core/fwd.hpp"
#include "../../core/include/epix/core/hierarchy.hpp"
#include "../../core/include/epix/core/label.hpp"
#include "../../core/include/epix/core/meta/fwd.hpp"
#include "../../core/include/epix/core/meta/typeid.hpp"
#include "../../core/include/epix/core/meta/typeindex.hpp"
#include "../../core/include/epix/core/query/access.hpp"
#include "../../core/include/epix/core/query/fetch.hpp"
#include "../../core/include/epix/core/query/filter.hpp"
#include "../../core/include/epix/core/query/fwd.hpp"
#include "../../core/include/epix/core/query/iter.hpp"
#include "../../core/include/epix/core/query/query.hpp"
#include "../../core/include/epix/core/query/state.hpp"
#include "../../core/include/epix/core/removal_detection.hpp"
#include "../../core/include/epix/core/schedule/schedule.hpp"
#include "../../core/include/epix/core/schedule/system_dispatcher.hpp"
#include "../../core/include/epix/core/schedule/system_set.hpp"
#include "../../core/include/epix/core/storage/bit_vector.hpp"
#include "../../core/include/epix/core/storage/blob_vec.hpp"
#include "../../core/include/epix/core/storage/sparse_set.hpp"
#include "../../core/include/epix/core/storage/table.hpp"
#include "../../core/include/epix/core/system/commands.hpp"
#include "../../core/include/epix/core/system/from_param.hpp"
#include "../../core/include/epix/core/system/param.hpp"
#include "../../core/include/epix/core/system/system.hpp"
#include "../../core/include/epix/core/tick.hpp"
#include "../../core/include/epix/core/type_system/fwd.hpp"
#include "../../core/include/epix/core/type_system/type_registry.hpp"
#include "../../core/include/epix/core/world.hpp"
#include "../../core/include/epix/core/world/command.hpp"
#include "../../core/include/epix/core/world/command_queue.hpp"
#include "../../core/include/epix/core/world/entity_ref.hpp"
#include "../../core/include/epix/utils/async.h"

export module epix.core;

// Export all namespaces and their contents
export namespace epix {
    // Re-export everything from the core namespace
    using namespace ::epix::core;
    using namespace ::epix::core::prelude;
}

// Export specific namespaces individually for fine-grained imports
export namespace epix::core {
    // Export all symbols from epix::core namespace
    using namespace ::epix::core;
    
    // Export nested namespaces
    namespace app {
        using namespace ::epix::core::app;
    }
    
    namespace archetype {
        using namespace ::epix::core::archetype;
    }
    
    namespace bundle {
        using namespace ::epix::core::bundle;
    }
    
    namespace event {
        using namespace ::epix::core::event;
    }
    
    namespace hierarchy {
        using namespace ::epix::core::hierarchy;
    }
    
    namespace meta {
        using namespace ::epix::core::meta;
    }
    
    namespace query {
        using namespace ::epix::core::query;
    }
    
    namespace schedule {
        using namespace ::epix::core::schedule;
    }
    
    namespace storage {
        using namespace ::epix::core::storage;
    }
    
    namespace system {
        using namespace ::epix::core::system;
    }
    
    namespace type_system {
        using namespace ::epix::core::type_system;
    }
    
    namespace wrapper {
        using namespace ::epix::core::wrapper;
    }
    
    namespace prelude {
        using namespace ::epix::core::prelude;
    }
}

// Export prelude namespace
export namespace epix::prelude {
    using namespace ::epix::core::prelude;
}
