#pragma once

#include <epix/tasks/task.hpp>
#include <optional>

namespace epix::tasks {

/**
 * @brief Poll a Task<T> once — non-blocking.
 * Returns the result if already finished, else `std::nullopt`.
 * Matches `bevy_tasks::futures::now_or_never`.
 */
template <typename T>
std::optional<T> now_or_never(Task<T>& task) {
    if (task.is_finished()) {
        return task.block();
    }
    return std::nullopt;
}

inline bool now_or_never(Task<void>& task) {
    if (task.is_finished()) {
        task.block();
        return true;
    }
    return false;
}

/**
 * @brief Alias for `now_or_never` — matches `bevy_tasks::check_ready`.
 */
template <typename T>
std::optional<T> check_ready(Task<T>& task) {
    return now_or_never(task);
}

inline bool check_ready(Task<void>& task) { return now_or_never(task); }

}  // namespace epix::tasks
