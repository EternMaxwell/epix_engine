#include <chrono>
#include <concepts>
#include <condition_variable>
#include <format>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <ranges>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

void print(std::string_view message) {
    static std::mutex cout_mutex;
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << std::format("[{}] {}", std::this_thread::get_id(), message) << std::endl;
}

struct fixed_stack {
   public:
    fixed_stack(size_t size) : _size(size) { _data = new size_t[size]; }
    ~fixed_stack() {
        if (_data) {
            delete[] _data;
        }
    }
    fixed_stack(const fixed_stack&)            = delete;
    fixed_stack& operator=(const fixed_stack&) = delete;
    fixed_stack(fixed_stack&& other) noexcept : _data(other._data), _size(other._size) {
        other._data = nullptr;
        other._size = 0;
    }
    fixed_stack& operator=(fixed_stack&& other) noexcept {
        if (this != &other) {
            _data       = other._data;
            _size       = other._size;
            other._data = nullptr;
            other._size = 0;
        }
        return *this;
    }
    void push(size_t value) { _data[_size++] = value; }
    void push_back(size_t value) { push(value); }
    size_t back() const { return _data[_size - 1]; }
    bool empty() const { return _size == 0; }
    void pop_back() { --_size; }
    size_t pop() { return _data[--_size]; }
    size_t size() const { return _size; }

   private:
    size_t* _data;
    size_t _size;
};
struct fixed_async_stack {
   private:
    size_t _size;
};

namespace task_graph {
struct ThreadPool {
    ThreadPool(size_t numThreads) : stop(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queueMutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) {
            worker.join();
        }
    }
    size_t threadCount() const { return workers.size(); }

    template <typename F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }

   private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop;
};

using Task = std::function<void()>;

using TaskId = std::string;  // for simplicity, using string as task identifier

struct TaskSet {
    TaskId id;
    std::unordered_set<TaskId> depends;
    std::unordered_set<TaskId> successors;
    std::unordered_set<TaskId> parents;
    std::unordered_set<TaskId> children;

    std::unordered_set<TaskId> validated_depends;
    std::unordered_set<TaskId> validated_successors;
    std::unordered_set<TaskId> validated_parents;
    std::unordered_set<TaskId> validated_children;

    std::optional<Task> task;  // optional task to execute
};

struct TaskCache {
    struct SetInfo {
        TaskSet* set;
        std::vector<size_t> depends;
        std::vector<size_t> successors;
        std::vector<size_t> parents;
        std::vector<size_t> children;

        size_t depend_count;
        size_t child_count;
        bool entered;
        bool finished;
    };
    std::vector<SetInfo> sets;
    std::unordered_map<TaskId, size_t> set_map;
};

struct TaskSetConfig {
    TaskId id;
    std::optional<Task> task;

    std::unordered_set<TaskId> depends;
    std::unordered_set<TaskId> successors;
    std::unordered_set<TaskId> parents;
    std::unordered_set<TaskId> children;

    static TaskSetConfig fromTask(const TaskId& id, const Task& task) { return TaskSetConfig{id, task, {}, {}, {}}; }
    static TaskSetConfig fromId(const TaskId& id) { return TaskSetConfig{id, std::nullopt, {}, {}, {}}; }

    TaskSetConfig& after(const TaskId& id) {
        depends.insert(id);
        return *this;
    }
    TaskSetConfig& before(const TaskId& id) {
        successors.insert(id);
        return *this;
    }
    TaskSetConfig& in_set(const TaskId& id) {
        parents.insert(id);
        return *this;
    }
    TaskSetConfig& contains(const TaskId& id) {
        children.insert(id);
        return *this;
    }
};

struct TaskGraph {
    std::unordered_map<TaskId, TaskSet> task_sets;
    std::optional<TaskCache> cache;
    ThreadPool thread_pool;

    TaskGraph(size_t numThreads) : thread_pool(numThreads) {}

    void addTaskSet(TaskSetConfig config) {
        if (task_sets.contains(config.id)) {
            return;
        }
        TaskSet task_set;
        task_set.id   = config.id;
        task_set.task = config.task;

        task_set.depends    = std::move(config.depends);
        task_set.successors = std::move(config.successors);
        task_set.parents    = std::move(config.parents);
        task_set.children   = std::move(config.children);

        task_sets.emplace(config.id, std::move(task_set));

        cache.reset();
    }
    void removeTaskSet(const TaskId& id) {
        task_sets.erase(id);
        cache.reset();
    }
    void addDependency(const TaskId& from, const TaskId& to) {
        if (from == to || (!task_sets.contains(from) && !task_sets.contains(to))) {
            return;
        }
        if (auto it = task_sets.find(from); it != task_sets.end()) {
            it->second.successors.insert(to);
        }
        if (auto it = task_sets.find(to); it != task_sets.end()) {
            it->second.depends.insert(from);
        }
        cache.reset();
    }
    void removeDependency(const TaskId& from, const TaskId& to) {
        size_t removed_count = 0;
        if (auto it = task_sets.find(from); it != task_sets.end()) {
            removed_count += it->second.successors.erase(to);
        }
        if (auto it = task_sets.find(to); it != task_sets.end()) {
            removed_count += it->second.depends.erase(from);
        }
        if (removed_count > 0) {
            cache.reset();
        }
    }
    void addHierarchy(const TaskId& parent, const TaskId& child) {
        if (parent == child || (!task_sets.contains(parent) && !task_sets.contains(child))) {
            return;
        }
        if (auto it = task_sets.find(parent); it != task_sets.end()) {
            it->second.children.insert(child);
        }
        if (auto it = task_sets.find(child); it != task_sets.end()) {
            it->second.parents.insert(parent);
        }
        cache.reset();
    }
    void removeHierarchy(const TaskId& parent, const TaskId& child) {
        size_t removed_count = 0;
        if (auto it = task_sets.find(parent); it != task_sets.end()) {
            removed_count += it->second.children.erase(child);
        }
        if (auto it = task_sets.find(child); it != task_sets.end()) {
            removed_count += it->second.parents.erase(parent);
        }
        if (removed_count > 0) {
            cache.reset();
        }
    }
    void validate() {
        for (auto&& [id, set] : task_sets) {
            set.validated_depends.clear();
            set.validated_successors.clear();
            set.validated_parents.clear();
            set.validated_children.clear();
            set.validated_depends.reserve(set.depends.size());
            set.validated_successors.reserve(set.successors.size());
            set.validated_parents.reserve(set.parents.size());
            set.validated_children.reserve(set.children.size());
        }

        for (auto&& [id, set] : task_sets) {
            for (const auto& dep : set.depends) {
                if (auto it = task_sets.find(dep); it != task_sets.end()) {
                    auto& dep_set = it->second;
                    set.validated_depends.insert(dep);
                    dep_set.validated_successors.insert(id);
                }
            }
            for (const auto& succ : set.successors) {
                if (auto it = task_sets.find(succ); it != task_sets.end()) {
                    auto& succ_set = it->second;
                    set.validated_successors.insert(succ);
                    succ_set.validated_depends.insert(id);
                }
            }
            for (const auto& parent : set.parents) {
                if (auto it = task_sets.find(parent); it != task_sets.end()) {
                    auto& parent_set = it->second;
                    set.validated_parents.insert(parent);
                    parent_set.validated_children.insert(id);
                }
            }
            for (const auto& child : set.children) {
                if (auto it = task_sets.find(child); it != task_sets.end()) {
                    auto& child_set = it->second;
                    set.validated_children.insert(child);
                    child_set.validated_parents.insert(id);
                }
            }
        }

        // TODO: Check for cycles in the graph
    }
    void simplify_validated() {
        // remove unnecessary dependencies - edges that has other same effect
        // path in dependency graph
        for (auto&& [id, set] : task_sets) {
            for (const auto& dep : set.validated_depends) {
                // using DFS to find other paths that is id -> dep
                std::unordered_set<TaskId> visited;
                std::function<bool(const TaskId&)> dfs = [&](const TaskId& current) {
                    if (current == dep) return true;              // found a path to dep
                    if (visited.contains(current)) return false;  // already visited
                    visited.insert(current);
                    if (auto it = task_sets.find(current); it != task_sets.end()) {
                        const auto& current_set = it->second;
                        for (const auto& next : current_set.validated_depends) {
                            if (dfs(next)) return true;  // continue DFS
                        }
                    }
                    return false;  // no path found
                };
                for (const auto& other_dep :
                     set.validated_depends | std::views::filter([&](const TaskId& other) { return other != dep; })) {
                    if (dfs(other_dep)) {
                        // found another path, remove this dependency
                        set.validated_depends.erase(dep);
                        if (auto it = task_sets.find(dep); it != task_sets.end()) {
                            it->second.validated_successors.erase(id);
                        }
                        break;  // no need to check other dependencies
                    }
                }
            }
        }
    }
    void rebuildCache() {
        cache.emplace();
        TaskCache& task_cache = *cache;
        task_cache.sets.reserve(task_sets.size());
        task_cache.set_map.reserve(task_sets.size());

        for (auto&& [id, set] : task_sets) {
            TaskCache::SetInfo& info = task_cache.sets.emplace_back();

            info.set = &set;

            info.depends.reserve(set.validated_depends.size());
            info.successors.reserve(set.validated_successors.size());
            info.parents.reserve(set.validated_parents.size());
            info.children.reserve(set.validated_children.size());

            info.depend_count = 0;
            info.child_count  = 0;
            info.entered      = false;
            info.finished     = false;

            task_cache.set_map[id] = task_cache.sets.size() - 1;
        }

        for (auto&& [index, info] : std::views::enumerate(task_cache.sets)) {
            for (const auto& dep_index : info.set->validated_depends | std::views::transform([this](const TaskId& id) {
                                             return cache->set_map.at(id);
                                         })) {
                info.depends.push_back(dep_index);
                task_cache.sets[dep_index].successors.push_back(index);
            }
            for (const auto& parent_index :
                 info.set->validated_parents |
                     std::views::transform([this](const TaskId& id) { return cache->set_map.at(id); })) {
                info.parents.push_back(parent_index);
                task_cache.sets[parent_index].children.push_back(index);
            }
        }
    }
    size_t setCount() const { return task_sets.size(); }
    size_t taskCount() const {
        size_t count = 0;
        for (const auto& [id, set] : task_sets) {
            if (set.task.has_value()) {
                count++;
            }
        }
        return count;
    }
    size_t depEdgeCount() const {
        size_t count = 0;
        for (auto&& [id, set] : task_sets) {
            count += set.validated_depends.size() + set.validated_successors.size();
        }
        return count / 2;
    }
    size_t hierarchyEdgeCount() const {
        size_t count = 0;
        for (auto&& [id, set] : task_sets) {
            count += set.validated_parents.size() + set.validated_children.size();
        }
        return count / 2;
    }
    void execute(std::function<void(Task&)> task_processor = [](Task& task) { task(); }, bool multi_threaded = true) {
        if (!cache) {
            validate();
            rebuildCache();
        }
        TaskCache& task_cache = *cache;

        size_t running_count   = 0;
        size_t remaining_count = task_cache.sets.size();

        std::vector<size_t> ready_stack;
        struct async_queue {
            mutable std::mutex mutex;
            std::condition_variable condition;
            std::vector<size_t> stack;

            void push(size_t index) {
                std::unique_lock<std::mutex> lock(mutex);
                stack.push_back(index);
                condition.notify_one();
            }

            std::vector<size_t> pop() {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this] { return !stack.empty(); });
                return std::move(stack);
            }
            std::vector<size_t> try_pop() {
                std::unique_lock<std::mutex> lock(mutex);
                return std::move(stack);
            }

            size_t size() const {
                std::unique_lock<std::mutex> lock(mutex);
                return stack.size();
            }

            bool empty() const {
                std::unique_lock<std::mutex> lock(mutex);
                return stack.empty();
            }
        } finished_stack;
        ready_stack.reserve(task_cache.sets.size());
        finished_stack.stack.reserve(task_cache.sets.size());

        std::function<void(size_t)> process_task = [&](size_t index) {
            // dispatch a task for execution
            TaskCache::SetInfo& info = task_cache.sets[index];
            if (multi_threaded) {
                thread_pool.enqueue([&, index] {
                    task_processor(info.set->task.value());
                    finished_stack.push(index);
                });
            } else {
                task_processor(info.set->task.value());
                finished_stack.push(index);
            }
            running_count++;
        };
        std::function<void()> enter_ready = [&]() {
            bool new_entered = true;
            while (!ready_stack.empty()) {
                size_t index = ready_stack.back();
                ready_stack.pop_back();
                auto& info   = task_cache.sets[index];
                info.entered = true;
                if (info.set->task) {
                    process_task(index);
                    // info.child_count++;
                } else {
                    if (info.child_count == 0) {
                        info.finished = true;
                        finished_stack.push(index);
                    }
                }
                for (auto&& child_index : info.children) {
                    auto& child_info = task_cache.sets[child_index];
                    child_info.depend_count--;
                    if (child_info.depend_count == 0) {
                        ready_stack.push_back(child_index);
                    }
                }
            }
        };

        // initialize cache value.
        for (auto&& [index, info] : std::views::enumerate(task_cache.sets)) {
            info.depend_count = info.depends.size() + info.parents.size();
            info.child_count  = info.children.size() + (info.set->task ? 1 : 0);
            info.entered      = false;
            info.finished     = false;
            if (info.depend_count == 0) {
                // no dependencies, can run immediately
                ready_stack.push_back(index);
            }
        }
        do {
            enter_ready();
            auto finishes = finished_stack.try_pop();
            if (finishes.empty()) {
                if (running_count == 0) {
                    // no running tasks, but no finished tasks, deadlock
                    break;
                }
                // wait for some tasks to finish
                finishes = finished_stack.pop();
            }

            for (auto&& finished_index : finishes) {
                auto& info = task_cache.sets[finished_index];
                if (info.child_count != 0) {
                    // this finished index is pushed by executable task, so the
                    // set is not really finished
                    running_count--;
                    info.child_count--;
                    if (info.child_count != 0) {
                        // still has children, set not finished
                        continue;
                    }
                }
                info.finished = true;
                remaining_count--;
                for (auto&& parent_index : info.parents) {
                    auto& parent_info = task_cache.sets[parent_index];
                    parent_info.child_count--;
                    if (parent_info.child_count == 0) {
                        finished_stack.push(parent_index);
                    }
                }
                for (auto&& successor_index : info.successors) {
                    auto& successor_info = task_cache.sets[successor_index];
                    successor_info.depend_count--;
                    if (successor_info.depend_count == 0) {
                        ready_stack.push_back(successor_index);
                    }
                }
            }
        } while (true);
        if (remaining_count > 0) {
            print("Some tasks are not executed, check for cycles in the graph.");

            for (const auto& set_info : task_cache.sets) {
                if (!set_info.entered) {
                    print(std::format(
                        "Set {} is not entered. with dependencies: {} "
                        "unfinished, "
                        "parents: {} unentered.",
                        set_info.set->id,
                        set_info.depends |
                            std::views::filter([&](size_t dep_index) { return !task_cache.sets[dep_index].finished; }) |
                            std::views::transform([&](size_t dep_index) { return task_cache.sets[dep_index].set->id; }),
                        set_info.parents | std::views::filter([&](size_t parent_index) {
                            return !task_cache.sets[parent_index].entered;
                        }) | std::views::transform([&](size_t parent_index) {
                            return task_cache.sets[parent_index].set->id;
                        })));
                } else if (!set_info.finished) {
                    print(
                        std::format("Set {} is entered but not finished. "
                                    "children: {} unfinished.",
                                    set_info.set->id, set_info.children | std::views::filter([&](size_t child_index) {
                                                          return !task_cache.sets[child_index].finished;
                                                      }) | std::views::transform([&](size_t child_index) {
                                                          return task_cache.sets[child_index].set->id;
                                                      })));
                }
            }
        } else {
            print("All tasks executed successfully.");
        }
    }
};
}  // namespace task_graph

namespace old_graph {
using TaskId = std::string;
using Task   = std::function<void()>;
struct TaskNode {
    TaskId id;
    Task task;
    std::unordered_set<TaskId> dependencies;
    std::unordered_set<TaskId> successors;

    std::unordered_set<TaskId> validated_dependencies;
    std::unordered_set<TaskId> validated_successors;
};
struct TaskConfig {
    TaskId id;
    Task task;
    std::unordered_set<TaskId> dependencies;
    std::unordered_set<TaskId> successors;

    TaskConfig(const TaskId& id, const Task& task) : id(id), task(task) {}
    TaskConfig& after(const TaskId& dep) {
        dependencies.insert(dep);
        return *this;
    }
    TaskConfig& before(const TaskId& succ) {
        successors.insert(succ);
        return *this;
    }
};
struct TaskCache {
    struct TaskInfo {
        TaskNode* node;
        std::vector<size_t> dependencies;
        std::vector<size_t> successors;

        size_t depend_count;
    };
    std::vector<TaskInfo> tasks;
    std::unordered_map<TaskId, size_t> task_map;
};
struct TaskGraph {
    std::unordered_map<TaskId, TaskNode> tasks;
    std::optional<TaskCache> cache;
    task_graph::ThreadPool thread_pool;

    TaskGraph(size_t numThreads) : thread_pool(numThreads) {}

    TaskGraph(const task_graph::TaskGraph& set_graph) : thread_pool(set_graph.thread_pool.threadCount()) {
        for (const auto& [id, set] :
             set_graph.task_sets | std::views::filter([](const auto& pair) { return pair.second.task.has_value(); })) {
            TaskConfig config(id, set.task.value());
            config.dependencies =
                set.depends | std::views::filter([&](const TaskId& dep) {
                    return set_graph.task_sets.contains(dep) && set_graph.task_sets.at(dep).task.has_value();
                }) |
                std::ranges::to<std::unordered_set<TaskId>>();
            config.successors =
                set.successors | std::views::filter([&](const TaskId& succ) {
                    return set_graph.task_sets.contains(succ) && set_graph.task_sets.at(succ).task.has_value();
                }) |
                std::ranges::to<std::unordered_set<TaskId>>();
            addTask(config);
        }
        std::unordered_map<TaskId, std::unordered_set<TaskId>> direct_child_ids;
        for (const auto& [id, set] : set_graph.task_sets) {
            for (const auto& child : set.children) {
                if (set_graph.task_sets.contains(child)) {
                    direct_child_ids[id].insert(child);
                }
            }
            for (const auto& parent : set.parents) {
                if (set_graph.task_sets.contains(parent)) {
                    direct_child_ids[parent].insert(id);
                }
            }
        }
        std::unordered_map<TaskId, std::unordered_set<TaskId>> all_child_ids;
        for (const auto& [id, _] : set_graph.task_sets) {
            std::unordered_set<TaskId> visited;
            std::function<void(const TaskId&)> dfs = [&](const TaskId& current) {
                if (visited.contains(current)) return;
                visited.insert(current);
                if (direct_child_ids.contains(current)) {
                    for (const auto& child : direct_child_ids.at(current)) {
                        if (set_graph.task_sets.at(child).task.has_value()) {
                            all_child_ids[id].insert(child);
                        }
                        dfs(child);
                    }
                }
            };
            dfs(id);
        }
        // add dependencies and successors of the parent id to all child ids;
        for (const auto& [id, children] : all_child_ids) {
            for (const auto& dependencies :
                 set_graph.task_sets.at(id).depends |
                     std::views::filter([&](const TaskId& dep) { return set_graph.task_sets.contains(dep); })) {
                if (set_graph.task_sets.at(dependencies).task.has_value()) {
                    for (const auto& child : children) {
                        addDependency(dependencies, child);
                    }
                }
                for (const auto& dep_child : all_child_ids[dependencies]) {
                    addDependency(dep_child, id);
                    for (const auto& child : children) {
                        addDependency(dep_child, child);
                    }
                }
            }
            for (const auto& successors :
                 set_graph.task_sets.at(id).successors |
                     std::views::filter([&](const TaskId& succ) { return set_graph.task_sets.contains(succ); })) {
                if (set_graph.task_sets.at(successors).task.has_value()) {
                    for (const auto& child : children) {
                        addDependency(child, successors);
                    }
                }
                for (const auto& succ_child : all_child_ids[successors]) {
                    addDependency(id, succ_child);
                    for (const auto& child : children) {
                        addDependency(child, succ_child);
                    }
                }
            }
        }
    }

    void addTask(TaskConfig config) {
        if (tasks.contains(config.id)) {
            return;  // Task already exists
        }
        TaskNode node;
        node.id           = config.id;
        node.task         = config.task;
        node.dependencies = std::move(config.dependencies);
        node.successors   = std::move(config.successors);

        tasks.emplace(config.id, std::move(node));
        cache.reset();
    }

    void removeTask(const TaskId& id) {
        if (tasks.erase(id)) {
            cache.reset();
        }
    }

    void addDependency(const TaskId& from, const TaskId& to) {
        size_t added_count = 0;
        if (auto it = tasks.find(from); it != tasks.end()) {
            added_count += it->second.successors.insert(to).second;
        }
        if (auto it = tasks.find(to); it != tasks.end()) {
            added_count += it->second.dependencies.insert(from).second;
        }
        if (added_count > 0) {
            cache.reset();
        }
    }

    void removeDependency(const TaskId& from, const TaskId& to) {
        size_t removed_count = 0;
        if (auto it = tasks.find(from); it != tasks.end()) {
            removed_count += it->second.successors.erase(to);
        }
        if (auto it = tasks.find(to); it != tasks.end()) {
            removed_count += it->second.dependencies.erase(from);
        }
        if (removed_count > 0) {
            cache.reset();
        }
    }

    size_t taskCount() const { return tasks.size(); }
    size_t edgeCount() const {
        size_t count = 0;
        for (const auto& [id, node] : tasks) {
            count += node.validated_dependencies.size() + node.validated_successors.size();
        }
        return count / 2;  // Each edge is counted twice
    }

    void validate() {
        for (auto&& [id, node] : tasks) {
            node.validated_dependencies.clear();
            node.validated_successors.clear();
            node.validated_dependencies.reserve(node.dependencies.size());
            node.validated_successors.reserve(node.successors.size());
        }

        for (auto&& [id, node] : tasks) {
            for (const auto& dep : node.dependencies) {
                if (auto it = tasks.find(dep); it != tasks.end()) {
                    auto& dep_node = it->second;
                    node.validated_dependencies.insert(dep);
                    dep_node.validated_successors.insert(id);
                }
            }
            for (const auto& succ : node.successors) {
                if (auto it = tasks.find(succ); it != tasks.end()) {
                    auto& succ_node = it->second;
                    node.validated_successors.insert(succ);
                    succ_node.validated_dependencies.insert(id);
                }
            }
        }
    }
    void simplify_validated() {
        // remove unnecessary dependencies - edges that has other same effect
        // path in dependency graph
        for (auto&& [id, node] : tasks) {
            for (const auto& dep : node.validated_dependencies) {
                // using DFS to find other paths that is id -> dep
                std::unordered_set<TaskId> visited;
                std::function<bool(const TaskId&)> dfs = [&](const TaskId& current) {
                    if (current == dep) return true;              // found a path to dep
                    if (visited.contains(current)) return false;  // already visited
                    visited.insert(current);
                    if (auto it = tasks.find(current); it != tasks.end()) {
                        const auto& current_node = it->second;
                        for (const auto& next : current_node.validated_dependencies) {
                            if (dfs(next)) return true;  // continue DFS
                        }
                    }
                    return false;  // no path found
                };
                for (const auto& other_dep : node.validated_dependencies | std::views::filter([&](const TaskId& other) {
                                                 return other != dep;
                                             })) {
                    if (dfs(other_dep)) {
                        // found another path, remove this dependency
                        node.validated_dependencies.erase(dep);
                        if (auto it = tasks.find(dep); it != tasks.end()) {
                            it->second.validated_successors.erase(id);
                        }
                        break;  // no need to check other dependencies
                    }
                }
            }
        }
    }
    void rebuildCache() {
        cache.emplace();
        TaskCache& task_cache = *cache;
        task_cache.tasks.reserve(tasks.size());
        task_cache.task_map.reserve(tasks.size());

        for (auto&& [id, node] : tasks) {
            TaskCache::TaskInfo& info = task_cache.tasks.emplace_back();

            info.node = &node;

            info.dependencies.reserve(node.validated_dependencies.size());
            info.successors.reserve(node.validated_successors.size());

            info.depend_count = 0;

            task_cache.task_map.emplace(id, task_cache.tasks.size() - 1);
        }

        for (auto&& [index, info] : std::views::enumerate(task_cache.tasks)) {
            for (const auto& dep_index :
                 info.node->validated_dependencies |
                     std::views::transform([this](const TaskId& id) { return cache->task_map.at(id); })) {
                info.dependencies.push_back(dep_index);
                task_cache.tasks[dep_index].successors.push_back(index);
            }
        }
    }
    void execute(std::function<void(Task&)> task_processor = [](Task& task) { task(); }, bool multi_threaded = true) {
        if (!cache) {
            validate();
            rebuildCache();
        }

        TaskCache& task_cache  = *cache;
        size_t running_count   = 0;
        size_t remaining_count = task_cache.tasks.size();
        struct async_queue {
            std::mutex mutex;
            std::condition_variable condition;
            std::vector<size_t> stack;

            void push(size_t index) {
                std::unique_lock<std::mutex> lock(mutex);
                stack.push_back(index);
                condition.notify_one();
            }

            size_t pop() {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this] { return !stack.empty(); });
                size_t index = stack.back();
                stack.pop_back();
                return index;
            }
        } finished_stack;
        finished_stack.stack.clear();
        finished_stack.stack.reserve(task_cache.tasks.size());
        std::function<void(size_t)> process_task = [&](size_t index) {
            // dispatch a task for execution
            TaskCache::TaskInfo& info = task_cache.tasks[index];
            if (multi_threaded) {
                thread_pool.enqueue([&, index] {
                    task_processor(info.node->task);
                    finished_stack.push(index);
                });
            } else {
                task_processor(info.node->task);
                finished_stack.push(index);
            }
            running_count++;
        };
        for (auto&& [index, info] : std::views::enumerate(task_cache.tasks)) {
            info.depend_count = info.dependencies.size();
            if (info.depend_count == 0) {
                // no dependencies, can run immediately
                process_task(index);
            }
        }
        while (running_count > 0) {
            size_t finished_index = finished_stack.pop();
            running_count--;
            remaining_count--;
            auto& info = task_cache.tasks[finished_index];
            for (auto&& successor_index : info.successors) {
                auto& successor_info = task_cache.tasks[successor_index];
                successor_info.depend_count--;
                if (successor_info.depend_count == 0) {
                    process_task(successor_index);
                }
            }
        }
        if (remaining_count > 0) {
            print("Some tasks are not executed, check for cycles in the graph.");
        } else {
            print("All tasks executed successfully.");
        }
    }
};
}  // namespace old_graph

std::function<void()> make_task(const std::string& name, double duration) {
    return [name, duration]() {
        print(std::format("Task {} started", name));
        std::this_thread::sleep_for(std::chrono::duration<double>(duration));
        print(std::format("Task {} finished", name));
    };
}

task_graph::TaskGraph* random_generate(size_t count, double task_prob, double dep_prob, double hier_prob) {
    using namespace task_graph;

    dep_prob = std::sqrt(dep_prob);
    // make it more likely to have dependencies

    TaskGraph* taskGraph = new TaskGraph(8);

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);  // probability
    std::uniform_real_distribution<double> dur_dist(0.01, 0.1);  // duration

    std::vector<std::vector<std::optional<TaskSetConfig>>> taskSetConfigs(
        count, std::vector<std::optional<TaskSetConfig>>(count));
    std::vector<std::pair<size_t, size_t>> remainging_slots =
        std::views::cartesian_product(std::views::iota(0u, count), std::views::iota(0u, count)) |
        std::ranges::to<std::vector<std::pair<size_t, size_t>>>();
    std::shuffle(remainging_slots.begin(), remainging_slots.end(), rng);
    remainging_slots.resize(count);
    std::sort(remainging_slots.begin(), remainging_slots.end(), [](const auto& a, const auto& b) {
        return a.first < b.first || (a.first == b.first && a.second < b.second);
    });
    size_t taskCount = 0;
    size_t setCount  = 0;
    for (auto&& [i, j] : remainging_slots) {
        if (prob_dist(rng) < task_prob) {
            taskSetConfigs[i][j] = TaskSetConfig::fromTask(std::format("task{}-{}.{}", taskCount, i, j),
                                                           make_task(std::format("task{}", taskCount), dur_dist(rng)));
            taskCount++;
        } else {
            taskSetConfigs[i][j] = TaskSetConfig::fromId(std::format("set{}-{}.{}", setCount, i, j));
            setCount++;
        }
    }

    // from top left to bottom right, add hierarchy.
    for (auto&& [x, y] : std::views::cartesian_product(std::views::iota(0u, count), std::views::iota(0u, count)) |
                             std::views::filter([&](const std::pair<size_t, size_t>& p) {
                                 return taskSetConfigs[p.first][p.second].has_value();
                             })) {
        for (auto&& [ox, oy] : std::views::cartesian_product(std::views::iota(x + 1, count), std::views::iota(0u, y)) |
                                   std::views::filter([&](const std::pair<size_t, size_t>& p) {
                                       return taskSetConfigs[p.first][p.second].has_value();
                                   })) {
            if (prob_dist(rng) < hier_prob) {
                taskSetConfigs[x][y]->contains(taskSetConfigs[ox][oy]->id);  // add hierarchy
            }
        }
        taskGraph->addTaskSet(taskSetConfigs[x][y].value());  // add task set
    }

    std::unordered_map<TaskId, std::unordered_set<TaskId>> all_child_ids;
    std::function<void(const TaskId&)> dfs = [&](const TaskId& current) {
        if (all_child_ids.contains(current)) return;  // already visited
        all_child_ids[current] = {};
        for (const auto& child : taskGraph->task_sets[current].children) {
            dfs(child);
            all_child_ids[current].insert(child);
            all_child_ids[current].insert(all_child_ids[child].begin(),
                                          all_child_ids[child].end());  // add all children
        }
    };
    for (auto&& [id, set] : taskGraph->task_sets) {
        dfs(id);  // find all children for each set
    }

    std::function<bool(const TaskId&, const TaskId&)> can_add_dependency = [&](const TaskId& from, const TaskId& to) {
        if (from == to) return false;                        // no self-dependency
        if (all_child_ids[from].contains(to)) return false;  // already a child
        if (all_child_ids[to].contains(from)) return false;  // already a parent
        for (const auto& child : all_child_ids[from]) {
            if (all_child_ids[to].contains(child)) {
                return false;
            }
        }
        // for all to's successors, can_add_dependency(from, succ) must be
        // true as well.
        // using dfs to check all recursive successors
        std::unordered_set<TaskId> visited;
        std::function<bool(const TaskId&)> dfs = [&](const TaskId& current) {
            if (visited.contains(current)) return true;
            visited.insert(current);
            if (current == from) return false;
            if (all_child_ids[current].contains(to)) return false;
            if (all_child_ids[to].contains(current)) return false;
            for (const auto& child : all_child_ids[current]) {
                if (all_child_ids[to].contains(child)) {
                    return false;
                }
            }
            for (const auto& succ : taskGraph->task_sets[current].successors) {
                if (!dfs(succ)) return false;
            }
            return true;  // no cycle found
        };
        if (!dfs(to)) return false;  // cycle found
        // for all from's dependencies, can_add_dependency(dep, to) must
        // be true as well.
        visited.clear();
        dfs = [&](const TaskId& current) {
            if (visited.contains(current)) return true;
            visited.insert(current);
            if (current == to) return false;
            if (all_child_ids[current].contains(from)) return false;
            if (all_child_ids[from].contains(current)) return false;
            for (const auto& child : all_child_ids[current]) {
                if (all_child_ids[from].contains(child)) {
                    return false;
                }
            }
            for (const auto& dep : taskGraph->task_sets[current].depends) {
                if (!dfs(dep)) return false;
            }
            return true;  // no cycle found
        };
        if (!dfs(from)) return false;
        return true;
    };

    // from bottom left to top right, add dependencies.
    for (auto&& [x, y] : std::views::cartesian_product(std::views::iota(0u, count), std::views::iota(0u, count)) |
                             std::views::filter([&](const std::pair<size_t, size_t>& p) {
                                 return taskSetConfigs[p.first][p.second].has_value();
                             })) {
        for (auto&& [ox, oy] :
             std::views::cartesian_product(std::views::iota(x + 1, count), std::views::iota(y + 1, count)) |
                 std::views::filter([&](const std::pair<size_t, size_t>& p) {
                     return taskSetConfigs[p.first][p.second].has_value();
                 })) {
            if (prob_dist(rng) < dep_prob && can_add_dependency(taskSetConfigs[ox][oy]->id, taskSetConfigs[x][y]->id)) {
                taskGraph->addDependency(taskSetConfigs[ox][oy]->id, taskSetConfigs[x][y]->id);
            }
        }
    }

    return taskGraph;
}

int main() {
    // here is just a showcase of core functionality, not a real test suite.
    std::unique_ptr<task_graph::TaskGraph> randomGraph(random_generate(1000, 0.9, 0.2, 0.005));
    size_t loop_count = 10;
    // 1000， 0.9， 0.2， 0.01 - hier mostly
    // 1000， 0.9， 0.2， 0.0005 - dep mostly
    // 1000， 0.9， 0.2， 0.005 - balance
    print("Set Graph");
    randomGraph->validate();            // complete edges
    randomGraph->simplify_validated();  // optional, to remove unnecessary dependencies
    randomGraph->rebuildCache();        // rebuild cache for execution, cache is used
                                        // to accelerate execution
    // these three function above is optional. validate and rebuildCache are called
    // automatically in execute if no cache exists.

    print(
        std::format("Set Graph: Set count: {}, Task count: {}, Dep edge count: {}, "
                    "Hierarchy edge count: {}",
                    randomGraph->setCount(), randomGraph->taskCount(), randomGraph->depEdgeCount(),
                    randomGraph->hierarchyEdgeCount()));
    {
        auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < loop_count; i++) {
            randomGraph->execute([](task_graph::Task&) {}, false);
        }
        auto end                                          = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        print(std::format("Set Graph Execution Time: {} ms", elapsed.count() / loop_count));
    }

    std::unique_ptr<old_graph::TaskGraph> oldGraph(new old_graph::TaskGraph(*randomGraph));
    print("Old Graph");
    oldGraph->validate();
    print(std::format("Old Graph: Task count: {}, Edge count: {}", oldGraph->taskCount(), oldGraph->edgeCount()));
    oldGraph->rebuildCache();
    {
        auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < loop_count; i++) {
            oldGraph->execute([](old_graph::Task& task) {}, false);
        }
        auto end                                          = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        print(std::format("Old Graph Execution Time: {} ms", elapsed.count() / loop_count));
    }
    oldGraph->simplify_validated();
    print("Old Graph Simplified");
    print(std::format("Old Graph Simplified: Task count: {}, Edge count: {}", oldGraph->taskCount(),
                      oldGraph->edgeCount()));
    oldGraph->rebuildCache();
    {
        auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < loop_count; i++) {
            oldGraph->execute([](old_graph::Task& task) {}, false);
        }
        auto end                                          = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        print(std::format("Old Graph Execution Time: {} ms", elapsed.count() / loop_count));
    }
    return 0;
}