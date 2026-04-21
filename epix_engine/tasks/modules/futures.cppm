module;

#ifndef EPIX_IMPORT_STD
#include <optional>
#endif
export module epix.tasks:futures;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :task;

namespace epix::tasks {

/**
 * @brief Poll a Task<T> once — non-blocking.
 * Returns the result if already finished, else `std::nullopt`.
 * Matches `bevy_tasks::futures::now_or_never`.
 */
export template <typename T>
std::optional<T> now_or_never(Task<T>& task) {
    if (task.is_finished()) {
        return task.block();
    }
    return std::nullopt;
}

export bool now_or_never(Task<void>& task) {
    if (task.is_finished()) {
        task.block();
        return true;
    }
    return false;
}

/**
 * @brief Alias for `now_or_never` — matches `bevy_tasks::check_ready`.
 */
export template <typename T>
std::optional<T> check_ready(Task<T>& task) {
    return now_or_never(task);
}

export bool check_ready(Task<void>& task) { return now_or_never(task); }

}  // namespace epix::tasks
