module;
export module epix.assets:io.memory;
import std;
import epix.meta;
import epix.utils;

export namespace assets {
export namespace memory {

export struct NotFoundError {
    std::error_code code;
    std::filesystem::path path;
};

export struct IoError {
    std::error_code code;
    std::filesystem::path path;
};

export struct ExceptionError {
    std::exception_ptr cause;
    std::filesystem::path path;
};

export using DirectoryError = std::variant<NotFoundError, IoError, ExceptionError>;

export enum class DirEventType { FileAdded, FileModified, FileRemoved, DirAdded, DirRemoved, Moved };

export struct DirEvent {
    DirEventType type;
    std::filesystem::path path;                     // target/new path, always relative to virtual root
    std::optional<std::filesystem::path> old_path;  // source/old path for moves, relative to virtual root
};

// Public value type: either a read-only view (std::span) or an owned shared buffer
export struct Value {
    std::variant<std::span<const std::uint8_t>, std::shared_ptr<std::vector<std::uint8_t>>> v;
    static Value from_span(std::span<const std::uint8_t> s) { return Value{.v = s}; }
    static Value from_shared(std::shared_ptr<std::vector<std::uint8_t>> b) { return Value{.v = b}; }
};

export struct Data {
    std::filesystem::path path;  // path of this file relative to the virtual root
    Value value;
};

// forward declare Directory for recursive node variant
export class Directory;

// Internal storage converts any incoming Value into a node map so stored Data is stable
using Node = std::variant<Data, Directory>;

struct DirectoryInternal {
    std::map<std::string, Node> nodes;
    // fields for event subscription and callbacks
    // when add callback to an dir, it will also be added to subdirs
    // callbacks will be called and queue will be cleared in `poll_events()`
    std::unordered_map<
        std::uint64_t,
        std::shared_ptr<std::pair<std::function<void(const DirEvent&)>, utils::Mutex<std::deque<DirEvent>>>>>
        subscribers;
    std::shared_ptr<std::atomic<std::uint64_t>> next_subscriber_id{std::make_shared<std::atomic<std::uint64_t>>(0)};
    // path of this directory relative to virtual root.
    // Note that even this dir is the root dir, for user, the path is not necessarily be the virtual root;
    std::filesystem::path path;
};

/**
 * @brief An in-memory virtual directory that supports files and subdirectories,
 *        with event subscription and polling.
 *
 * The directory is in its backend an tree of std::map<string, Node> where Node
 * is either a file (Data) or a subdirectory (Directory). The directory supports
 * concurrent access and modifications from multiple threads, and provides event
 * callbacks and polling for changes. The directory paths are always relative to
 * a virtual root, which is specified when creating the directory.
 */
export class Directory {
   public:
    static Directory create(const std::filesystem::path& path) {
        Directory d;
        d.internal_                   = std::make_shared<utils::RwLock<DirectoryInternal>>();
        d.internal_->write().ref.path = path.lexically_normal();
        return d;
    }

    /** @brief Checks if a path exists, either as a file or directory */
    std::expected<bool, DirectoryError> exists(const std::filesystem::path& p) const;
    /** @brief Checks if a path exists and is a directory */
    std::expected<bool, DirectoryError> is_directory(const std::filesystem::path& p) const;
    /** @brief Get the data ref for a file at the specified path, or error if failed */
    std::expected<Data, DirectoryError> get_file(const std::filesystem::path& p) const;
    /** @brief Gets the Directory for a subdirectory at the specified path, or error if failed */
    std::expected<Directory, DirectoryError> get_directory(const std::filesystem::path& p) const;
    /** @brief Inserts or replaces a file at the specified path with the given value, or error if failed
     * @return The inserted data reference or error */
    std::expected<Value, DirectoryError> insert_file(const std::filesystem::path& p, Value v) const;
    /** @brief Inserts a file at the specified path with the given value if it doesn't already exist, or error if failed
     * @return The inserted data reference or error */
    std::expected<Value, DirectoryError> insert_file_if_new(const std::filesystem::path& p, Value v) const;
    /** @brief Removes a file at the specified path, or error if failed
     * @return The removed data reference or error */
    std::expected<Data, DirectoryError> remove_file(const std::filesystem::path& p) const;
    /** @brief Creates a new or replaces a directory at the specified path, or error if failed
     * @return The created directory or error */
    std::expected<Directory, DirectoryError> create_directory(const std::filesystem::path& p) const;
    /** @brief Creates a new directory at the specified path if it doesn't already exist, or error if failed
     * @return The created directory or error */
    std::expected<Directory, DirectoryError> create_directory_if_new(const std::filesystem::path& p) const;
    /** @brief Removes an empty directory at the specified path, or error if failed
     * @return The removed directory or error */
    std::expected<Directory, DirectoryError> remove_directory(const std::filesystem::path& p) const;
    /** @brief Moves a file or directory from `from` path to `to` path, or error if failed */
    std::expected<void, DirectoryError> move(const std::filesystem::path& from, const std::filesystem::path& to) const;
    /** @brief Lists the contents of a directory at the specified path, or error if failed
     * @param recursive If true, list all contents recursively, otherwise only direct children.
     * @return An iterable of paths relative to the current directory. */
    std::expected<utils::input_iterable<std::filesystem::path>, DirectoryError> list_directory(
        const std::filesystem::path& p, bool recursive) const;
    /** @brief Add a event callback and return its ID */
    std::uint64_t add_callback(std::function<void(const DirEvent&)> cb) const;
    /** @brief Remove a event callback by its ID */
    void remove_callback(std::uint64_t id) const;
    /** @brief Poll for and process any pending directory events recursively, and clear the event queue */
    void poll_events() const;
    /** @brief Get the path of this directory relative to the virtual root */
    std::filesystem::path get_path() const;

   private:
    std::shared_ptr<utils::RwLock<DirectoryInternal>> internal_;

    // ---- internal helpers -----------------------------------------------
    // Collect callbacks from an already-written DirectoryInternal (caller holds write lock)
    using SubPtr = std::shared_ptr<std::pair<std::function<void(const DirEvent&)>, utils::Mutex<std::deque<DirEvent>>>>;

    static std::vector<SubPtr> collect_subs(const DirectoryInternal& internal) {
        std::vector<SubPtr> subs;
        subs.reserve(internal.subscribers.size());
        for (auto& [id, sub] : internal.subscribers) subs.push_back(sub);
        return subs;
    }

    // Call callbacks directly (must be called WITHOUT holding any directory lock)
    static void fire(const std::vector<SubPtr>& subs, const DirEvent& ev) {
        for (auto& sub : subs) sub->first(ev);
    }

    // Recursively propagate subscriber to all sub-directories (write-locked by caller)
    static void propagate_subscriber(
        DirectoryInternal& internal,
        std::uint64_t id,
        std::shared_ptr<std::pair<std::function<void(const DirEvent&)>, utils::Mutex<std::deque<DirEvent>>>> sub) {
        for (auto& [name, node] : internal.nodes) {
            if (std::holds_alternative<Directory>(node)) {
                auto& subdir             = std::get<Directory>(node);
                auto subw                = subdir.internal_->write();
                subw.ref.subscribers[id] = sub;
                propagate_subscriber(subw.ref, id, sub);
            }
        }
    }

    // Recursively remove subscriber from all sub-directories (write-locked by caller)
    static void remove_subscriber(DirectoryInternal& internal, std::uint64_t id) {
        for (auto& [name, node] : internal.nodes) {
            if (std::holds_alternative<Directory>(node)) {
                auto& subdir = std::get<Directory>(node);
                auto subw    = subdir.internal_->write();
                subw.ref.subscribers.erase(id);
                remove_subscriber(subw.ref, id);
            }
        }
    }

    // Returns {parent_ptr, last_component} or error.
    std::expected<std::pair<std::shared_ptr<utils::RwLock<DirectoryInternal>>, std::string>, DirectoryError>
    resolve_parent(const std::filesystem::path& norm) const {
        auto parts = std::vector<std::string>{};
        for (auto& comp : norm) {
            auto s = comp.string();
            if (s.empty() || s == "." || s == "/") continue;
            parts.push_back(s);
        }
        if (parts.empty()) {
            return std::unexpected(IoError{std::error_code{}, norm});
        }
        auto current = internal_;
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            auto r  = current->read();
            auto it = r.ref.nodes.find(parts[i]);
            if (it == r.ref.nodes.end()) {
                return std::unexpected(NotFoundError{std::error_code{}, norm});
            }
            if (!std::holds_alternative<Directory>(it->second)) {
                return std::unexpected(NotFoundError{std::error_code{}, norm});
            }
            current = std::get<Directory>(it->second).internal_;
        }
        return std::make_pair(current, parts.back());
    }

    // Same as resolve_parent but also navigates the last component as a directory
    std::expected<std::shared_ptr<utils::RwLock<DirectoryInternal>>, DirectoryError> resolve_dir(
        const std::filesystem::path& norm) const {
        auto parts = std::vector<std::string>{};
        for (auto& comp : norm) {
            auto s = comp.string();
            if (s.empty() || s == "." || s == "/") continue;
            parts.push_back(s);
        }
        if (parts.empty()) return internal_;
        auto current = internal_;
        for (auto& part : parts) {
            auto r  = current->read();
            auto it = r.ref.nodes.find(part);
            if (it == r.ref.nodes.end()) {
                return std::unexpected(NotFoundError{std::error_code{}, norm});
            }
            if (!std::holds_alternative<Directory>(it->second)) {
                return std::unexpected(NotFoundError{std::error_code{}, norm});
            }
            current = std::get<Directory>(it->second).internal_;
        }
        return current;
    }
};

// ---- method implementations -------------------------------------------

std::expected<bool, assets::memory::DirectoryError> assets::memory::Directory::exists(
    const std::filesystem::path& p) const {
    auto norm = p.lexically_normal();
    try {
        auto res = resolve_parent(norm);
        if (!res.has_value()) {
            if (std::holds_alternative<NotFoundError>(res.error())) return false;
            return std::unexpected(res.error());
        }
        auto& [parent, name] = res.value();
        auto r               = parent->read();
        return r.ref.nodes.contains(name);
    } catch (...) {
        return std::unexpected(ExceptionError{std::current_exception(), p});
    }
}

std::expected<bool, assets::memory::DirectoryError> assets::memory::Directory::is_directory(
    const std::filesystem::path& p) const {
    auto norm = p.lexically_normal();
    try {
        auto parts = std::vector<std::string>{};
        for (auto& comp : norm) {
            auto s = comp.string();
            if (s.empty() || s == "." || s == "/") continue;
            parts.push_back(s);
        }
        if (parts.empty()) return true;

        auto res = resolve_parent(norm);
        if (!res.has_value()) return std::unexpected(res.error());
        auto& [parent, name] = res.value();
        auto r               = parent->read();
        auto it              = r.ref.nodes.find(name);
        if (it == r.ref.nodes.end()) return false;
        return std::holds_alternative<Directory>(it->second);
    } catch (...) {
        return std::unexpected(ExceptionError{std::current_exception(), p});
    }
}

std::expected<assets::memory::Data, assets::memory::DirectoryError> assets::memory::Directory::get_file(
    const std::filesystem::path& p) const {
    auto norm = p.lexically_normal();
    try {
        auto res = resolve_parent(norm);
        if (!res.has_value()) return std::unexpected(res.error());
        auto& [parent, name] = res.value();
        auto r               = parent->read();
        auto it              = r.ref.nodes.find(name);
        if (it == r.ref.nodes.end()) return std::unexpected(NotFoundError{std::error_code{}, norm});
        if (!std::holds_alternative<Data>(it->second)) return std::unexpected(NotFoundError{std::error_code{}, norm});
        return std::get<Data>(it->second);
    } catch (...) {
        return std::unexpected(ExceptionError{std::current_exception(), p});
    }
}

std::expected<assets::memory::Directory, assets::memory::DirectoryError> assets::memory::Directory::get_directory(
    const std::filesystem::path& p) const {
    auto norm = p.lexically_normal();
    try {
        auto res = resolve_dir(norm);
        if (!res.has_value()) return std::unexpected(res.error());
        Directory d;
        d.internal_ = res.value();
        return d;
    } catch (...) {
        return std::unexpected(ExceptionError{std::current_exception(), p});
    }
}

std::expected<assets::memory::Value, assets::memory::DirectoryError> assets::memory::Directory::insert_file(
    const std::filesystem::path& p, Value v) const {
    auto norm = p.lexically_normal();
    try {
        auto parts = std::vector<std::string>{};
        for (auto& comp : norm) {
            auto s = comp.string();
            if (s.empty() || s == "." || s == "/") continue;
            parts.push_back(s);
        }
        if (parts.empty()) return std::unexpected(IoError{std::error_code{}, norm});

        // Walk/create intermediate directories
        auto current      = internal_;
        auto current_path = internal_->read().ref.path;
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            auto child_internal = std::shared_ptr<utils::RwLock<DirectoryInternal>>{};
            {
                auto w  = current->write();
                auto it = w.ref.nodes.find(parts[i]);
                if (it == w.ref.nodes.end()) {
                    auto child_path = (current_path / parts[i]).lexically_normal();
                    Directory sub;
                    sub.internal_ = std::make_shared<utils::RwLock<DirectoryInternal>>();
                    {
                        auto sw                   = sub.internal_->write();
                        sw.ref.path               = child_path;
                        sw.ref.next_subscriber_id = w.ref.next_subscriber_id;
                        for (auto& [id, sp] : w.ref.subscribers) sw.ref.subscribers[id] = sp;
                    }
                    w.ref.nodes[parts[i]] = sub;
                    child_internal        = sub.internal_;
                    current_path          = child_path;
                } else if (!std::holds_alternative<Directory>(it->second)) {
                    return std::unexpected(IoError{std::error_code{}, norm});
                } else {
                    child_internal = std::get<Directory>(it->second).internal_;
                    current_path   = child_internal->read().ref.path;
                }
            }
            current = child_internal;
        }

        // Insert the file into current directory
        std::vector<SubPtr> subs;
        DirEvent de{};
        {
            auto w  = current->write();
            auto it = w.ref.nodes.find(parts.back());
            DirEventType ev_type;
            if (it == w.ref.nodes.end()) {
                ev_type = DirEventType::FileAdded;
            } else if (!std::holds_alternative<Data>(it->second)) {
                return std::unexpected(IoError{std::error_code{}, norm});
            } else {
                ev_type = DirEventType::FileModified;
            }
            std::shared_ptr<std::vector<std::uint8_t>> stored_buf;
            if (std::holds_alternative<std::span<const std::uint8_t>>(v.v)) {
                auto sp    = std::get<std::span<const std::uint8_t>>(v.v);
                stored_buf = std::make_shared<std::vector<std::uint8_t>>(sp.begin(), sp.end());
            } else {
                stored_buf = std::get<std::shared_ptr<std::vector<std::uint8_t>>>(v.v);
            }
            auto file_path            = (w.ref.path / parts.back()).lexically_normal();
            w.ref.nodes[parts.back()] = Data{file_path, Value::from_shared(stored_buf)};
            de                        = DirEvent{ev_type, file_path, std::nullopt};
            subs                      = collect_subs(w.ref);
        }  // write lock released
        fire(subs, de);
        return v;
    } catch (...) {
        return std::unexpected(ExceptionError{std::current_exception(), p});
    }
}

std::expected<assets::memory::Value, assets::memory::DirectoryError> assets::memory::Directory::insert_file_if_new(
    const std::filesystem::path& p, Value v) const {
    auto norm = p.lexically_normal();
    try {
        auto res = resolve_parent(norm);
        if (res.has_value()) {
            auto& [parent, name] = res.value();
            auto r               = parent->read();
            if (r.ref.nodes.contains(name)) {
                auto it = r.ref.nodes.find(name);
                if (std::holds_alternative<Data>(it->second)) return std::get<Data>(it->second).value;
                return std::unexpected(IoError{std::error_code{}, norm});
            }
        }
        return insert_file(p, std::move(v));
    } catch (...) {
        return std::unexpected(ExceptionError{std::current_exception(), p});
    }
}

std::expected<assets::memory::Data, assets::memory::DirectoryError> assets::memory::Directory::remove_file(
    const std::filesystem::path& p) const {
    auto norm = p.lexically_normal();
    try {
        auto res = resolve_parent(norm);
        if (!res.has_value()) return std::unexpected(res.error());
        auto& [parent, name] = res.value();
        Data removed;
        std::vector<SubPtr> subs;
        DirEvent de{};
        {
            auto w  = parent->write();
            auto it = w.ref.nodes.find(name);
            if (it == w.ref.nodes.end()) return std::unexpected(NotFoundError{std::error_code{}, norm});
            if (!std::holds_alternative<Data>(it->second))
                return std::unexpected(NotFoundError{std::error_code{}, norm});
            removed = std::get<Data>(it->second);
            subs    = collect_subs(w.ref);
            w.ref.nodes.erase(it);
            de = DirEvent{DirEventType::FileRemoved, removed.path, std::nullopt};
        }  // write lock released
        fire(subs, de);
        return removed;
    } catch (...) {
        return std::unexpected(ExceptionError{std::current_exception(), p});
    }
}

std::expected<assets::memory::Directory, assets::memory::DirectoryError> assets::memory::Directory::create_directory(
    const std::filesystem::path& p) const {
    auto norm = p.lexically_normal();
    try {
        auto parts = std::vector<std::string>{};
        for (auto& comp : norm) {
            auto s = comp.string();
            if (s.empty() || s == "." || s == "/") continue;
            parts.push_back(s);
        }
        if (parts.empty()) return get_directory(p);

        auto current = internal_;
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            auto child_internal = std::shared_ptr<utils::RwLock<DirectoryInternal>>{};
            {
                auto w  = current->write();
                auto it = w.ref.nodes.find(parts[i]);
                if (it == w.ref.nodes.end()) {
                    auto child_path = (w.ref.path / parts[i]).lexically_normal();
                    Directory sub;
                    sub.internal_ = std::make_shared<utils::RwLock<DirectoryInternal>>();
                    {
                        auto sw                   = sub.internal_->write();
                        sw.ref.path               = child_path;
                        sw.ref.next_subscriber_id = w.ref.next_subscriber_id;
                        for (auto& [id, sp] : w.ref.subscribers) sw.ref.subscribers[id] = sp;
                    }
                    w.ref.nodes[parts[i]] = sub;
                    child_internal        = sub.internal_;
                } else if (!std::holds_alternative<Directory>(it->second)) {
                    return std::unexpected(IoError{std::error_code{}, norm});
                } else {
                    child_internal = std::get<Directory>(it->second).internal_;
                }
            }
            current = child_internal;
        }

        Directory nd;
        std::vector<SubPtr> subs;
        DirEvent de{};
        {
            auto w      = current->write();
            auto it     = w.ref.nodes.find(parts.back());
            bool is_new = (it == w.ref.nodes.end());
            if (!is_new && std::holds_alternative<Data>(it->second))
                return std::unexpected(IoError{std::error_code{}, norm});
            auto new_path = (w.ref.path / parts.back()).lexically_normal();
            nd.internal_  = std::make_shared<utils::RwLock<DirectoryInternal>>();
            {
                auto nw                   = nd.internal_->write();
                nw.ref.path               = new_path;
                nw.ref.next_subscriber_id = w.ref.next_subscriber_id;
                for (auto& [id, sp] : w.ref.subscribers) nw.ref.subscribers[id] = sp;
            }
            w.ref.nodes[parts.back()] = nd;
            de                        = DirEvent{DirEventType::DirAdded, new_path, std::nullopt};
            subs                      = collect_subs(w.ref);
        }  // write lock released
        fire(subs, de);
        return nd;
    } catch (...) {
        return std::unexpected(ExceptionError{std::current_exception(), p});
    }
}

std::expected<assets::memory::Directory, assets::memory::DirectoryError>
assets::memory::Directory::create_directory_if_new(const std::filesystem::path& p) const {
    auto norm = p.lexically_normal();
    try {
        auto res = resolve_dir(norm);
        if (res.has_value()) {
            Directory d;
            d.internal_ = res.value();
            return d;
        }
        return create_directory(p);
    } catch (...) {
        return std::unexpected(ExceptionError{std::current_exception(), p});
    }
}

std::expected<assets::memory::Directory, assets::memory::DirectoryError> assets::memory::Directory::remove_directory(
    const std::filesystem::path& p) const {
    auto norm = p.lexically_normal();
    try {
        auto res = resolve_parent(norm);
        if (!res.has_value()) return std::unexpected(res.error());
        auto& [parent, name] = res.value();
        Directory removed_dir;
        std::vector<SubPtr> subs;
        DirEvent de{};
        {
            auto w  = parent->write();
            auto it = w.ref.nodes.find(name);
            if (it == w.ref.nodes.end()) return std::unexpected(NotFoundError{std::error_code{}, norm});
            if (!std::holds_alternative<Directory>(it->second))
                return std::unexpected(NotFoundError{std::error_code{}, norm});
            auto& dir = std::get<Directory>(it->second);
            auto dr   = dir.internal_->read();
            if (!dr.ref.nodes.empty()) return std::unexpected(IoError{std::error_code{}, norm});
            removed_dir       = dir;
            auto removed_path = dr.ref.path;
            subs              = collect_subs(w.ref);
            w.ref.nodes.erase(it);
            de = DirEvent{DirEventType::DirRemoved, removed_path, std::nullopt};
        }  // write lock released
        fire(subs, de);
        return removed_dir;
    } catch (...) {
        return std::unexpected(ExceptionError{std::current_exception(), p});
    }
}

std::expected<void, assets::memory::DirectoryError> assets::memory::Directory::move(
    const std::filesystem::path& from, const std::filesystem::path& to) const {
    auto norm_from = from.lexically_normal();
    auto norm_to   = to.lexically_normal();
    try {
        auto res_from = resolve_parent(norm_from);
        if (!res_from.has_value()) return std::unexpected(res_from.error());
        auto& [parent_from, name_from] = res_from.value();
        {
            auto r = parent_from->read();
            if (!r.ref.nodes.contains(name_from)) return std::unexpected(NotFoundError{std::error_code{}, norm_from});
        }

        // Ensure destination parent exists
        auto res_to = resolve_parent(norm_to);
        std::shared_ptr<utils::RwLock<DirectoryInternal>> parent_to;
        std::string name_to;
        if (!res_to.has_value()) {
            auto parent_path = norm_to.parent_path();
            if (!parent_path.empty()) {
                auto cd = create_directory_if_new(parent_path);
                if (!cd.has_value()) return std::unexpected(cd.error());
            }
            auto res2 = resolve_parent(norm_to);
            if (!res2.has_value()) return std::unexpected(res2.error());
            parent_to = res2.value().first;
            name_to   = res2.value().second;
        } else {
            parent_to = res_to.value().first;
            name_to   = res_to.value().second;
        }

        // Extract node from source
        Node moved_node;
        std::filesystem::path old_abs_path;
        {
            auto w  = parent_from->write();
            auto it = w.ref.nodes.find(name_from);
            if (it == w.ref.nodes.end()) return std::unexpected(NotFoundError{std::error_code{}, norm_from});
            if (std::holds_alternative<Data>(it->second))
                old_abs_path = std::get<Data>(it->second).path;
            else
                old_abs_path = std::get<Directory>(it->second).internal_->read().ref.path;
            moved_node = std::move(it->second);
            w.ref.nodes.erase(it);
        }

        // Compute new absolute path
        std::filesystem::path new_abs_path;
        {
            auto r_to    = parent_to->read();
            new_abs_path = (r_to.ref.path / name_to).lexically_normal();
        }
        if (std::holds_alternative<Data>(moved_node)) {
            std::get<Data>(moved_node).path = new_abs_path;
        } else {
            std::get<Directory>(moved_node).internal_->write().ref.path = new_abs_path;
        }

        // Insert into destination and collect subscribers
        std::vector<SubPtr> subs;
        DirEvent de{DirEventType::Moved, new_abs_path, old_abs_path};
        {
            auto w               = parent_to->write();
            w.ref.nodes[name_to] = std::move(moved_node);
            subs                 = collect_subs(w.ref);
        }  // write lock released
        // Also collect root subscribers if parent_to != internal_
        if (parent_to.get() != internal_.get()) {
            auto r = internal_->read();
            for (auto& [id, sub] : r.ref.subscribers) {
                bool already = false;
                for (auto& s : subs)
                    if (s == sub) {
                        already = true;
                        break;
                    }
                if (!already) subs.push_back(sub);
            }
        }
        fire(subs, de);
        return {};
    } catch (...) {
        return std::unexpected(ExceptionError{std::current_exception(), from});
    }
}

std::expected<utils::input_iterable<std::filesystem::path>, assets::memory::DirectoryError>
assets::memory::Directory::list_directory(const std::filesystem::path& p, bool recursive) const {
    auto norm = p.lexically_normal();
    try {
        auto dir_res = resolve_dir(norm);
        if (!dir_res.has_value()) return std::unexpected(dir_res.error());
        auto target = dir_res.value();
        auto r      = target->read();

        std::vector<std::filesystem::path> result;
        std::function<void(const DirectoryInternal&)> collect;
        collect = [&](const DirectoryInternal& internal) {
            for (auto& [name, node] : internal.nodes) {
                if (std::holds_alternative<Data>(node)) {
                    result.push_back(std::get<Data>(node).path);
                } else {
                    auto& subdir = std::get<Directory>(node);
                    auto sr      = subdir.internal_->read();
                    result.push_back(sr.ref.path);
                    if (recursive) collect(sr.ref);
                }
            }
        };
        collect(r.ref);
        return utils::input_iterable<std::filesystem::path>(std::move(result));
    } catch (...) {
        return std::unexpected(ExceptionError{std::current_exception(), p});
    }
}

std::uint64_t assets::memory::Directory::add_callback(std::function<void(const DirEvent&)> cb) const {
    auto w   = internal_->write();
    auto id  = w.ref.next_subscriber_id->fetch_add(1);
    auto sub = std::make_shared<std::pair<std::function<void(const DirEvent&)>, utils::Mutex<std::deque<DirEvent>>>>(
        std::move(cb), utils::Mutex<std::deque<DirEvent>>());
    w.ref.subscribers[id] = sub;
    propagate_subscriber(w.ref, id, sub);
    return id;
}

void assets::memory::Directory::remove_callback(std::uint64_t id) const {
    auto w = internal_->write();
    w.ref.subscribers.erase(id);
    remove_subscriber(w.ref, id);
}

void assets::memory::Directory::poll_events() const {
    // Callbacks are invoked directly by modifying operations.
    // This method is a no-op but kept for API compatibility.
    (void)this;
}

std::filesystem::path assets::memory::Directory::get_path() const { return internal_->read().ref.path; }

}  // namespace memory
}  // namespace assets
