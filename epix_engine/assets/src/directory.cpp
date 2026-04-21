module;
#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
#endif
module epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.utils;

namespace epix::assets::memory {

Directory Directory::create(const std::filesystem::path& path) {
    Directory d;
    d.internal_                   = std::make_shared<utils::RwLock<DirectoryInternal>>();
    d.internal_->write().ref.path = path.lexically_normal();
    return d;
}

std::vector<Directory::SubPtr> Directory::collect_subs(const DirectoryInternal& internal) {
    std::vector<SubPtr> subs;
    subs.reserve(internal.subscribers.size());
    for (auto& [id, sub] : internal.subscribers) subs.push_back(sub);
    return subs;
}

void Directory::propagate_subscriber(
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

void Directory::remove_subscriber(DirectoryInternal& internal, std::uint64_t id) {
    for (auto& [name, node] : internal.nodes) {
        if (std::holds_alternative<Directory>(node)) {
            auto& subdir = std::get<Directory>(node);
            auto subw    = subdir.internal_->write();
            subw.ref.subscribers.erase(id);
            remove_subscriber(subw.ref, id);
        }
    }
}

std::expected<std::pair<std::shared_ptr<utils::RwLock<DirectoryInternal>>, std::string>, DirectoryError>
Directory::resolve_parent(const std::filesystem::path& norm) const {
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

std::expected<std::shared_ptr<utils::RwLock<DirectoryInternal>>, DirectoryError> Directory::resolve_dir(
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

std::expected<bool, DirectoryError> Directory::exists(const std::filesystem::path& p) const {
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

std::expected<bool, DirectoryError> Directory::is_directory(const std::filesystem::path& p) const {
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

std::expected<Data, DirectoryError> Directory::get_file(const std::filesystem::path& p) const {
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

std::expected<Directory, DirectoryError> Directory::get_directory(const std::filesystem::path& p) const {
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

std::expected<Value, DirectoryError> Directory::insert_file(const std::filesystem::path& p, Value v) const {
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
            std::shared_ptr<std::vector<std::byte>> stored_buf;
            if (std::holds_alternative<std::span<const std::byte>>(v.v)) {
                auto sp    = std::get<std::span<const std::byte>>(v.v);
                stored_buf = std::make_shared<std::vector<std::byte>>(sp.begin(), sp.end());
            } else {
                stored_buf = std::get<std::shared_ptr<std::vector<std::byte>>>(v.v);
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

std::expected<Value, DirectoryError> Directory::insert_file_if_new(const std::filesystem::path& p, Value v) const {
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

std::expected<Data, DirectoryError> Directory::remove_file(const std::filesystem::path& p) const {
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

std::expected<Directory, DirectoryError> Directory::create_directory(const std::filesystem::path& p) const {
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

std::expected<Directory, DirectoryError> Directory::create_directory_if_new(const std::filesystem::path& p) const {
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

std::expected<Directory, DirectoryError> Directory::remove_directory(const std::filesystem::path& p) const {
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

std::expected<void, DirectoryError> Directory::move(const std::filesystem::path& from,
                                                    const std::filesystem::path& to) const {
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

std::expected<utils::input_iterable<std::filesystem::path>, DirectoryError> Directory::list_directory(
    const std::filesystem::path& p, bool recursive) const {
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

std::uint64_t Directory::add_callback(std::function<void(const DirEvent&)> cb) const {
    auto w   = internal_->write();
    auto id  = w.ref.next_subscriber_id->fetch_add(1);
    auto sub = std::make_shared<std::pair<std::function<void(const DirEvent&)>, utils::Mutex<std::deque<DirEvent>>>>(
        std::move(cb), utils::Mutex<std::deque<DirEvent>>());
    w.ref.subscribers[id] = sub;
    propagate_subscriber(w.ref, id, sub);
    return id;
}

void Directory::remove_callback(std::uint64_t id) const {
    auto w = internal_->write();
    w.ref.subscribers.erase(id);
    remove_subscriber(w.ref, id);
}

void Directory::poll_events() const { (void)this; }

std::filesystem::path Directory::get_path() const { return internal_->read().ref.path; }

}  // namespace epix::assets::memory
