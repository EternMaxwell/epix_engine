module;
#ifndef EPIX_IMPORT_STD
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#endif
export module epix.assets:io.memory;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.meta;
import epix.utils;

namespace epix::assets {
namespace memory {

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
    std::variant<std::span<const std::byte>, std::shared_ptr<std::vector<std::byte>>> v;
    static Value from_span(std::span<const std::byte> s) { return Value{.v = s}; }
    static Value from_shared(std::shared_ptr<std::vector<std::byte>> b) { return Value{.v = b}; }
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
    static Directory create(const std::filesystem::path& path);

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

    static std::vector<SubPtr> collect_subs(const DirectoryInternal& internal);

    // Call callbacks directly (must be called WITHOUT holding any directory lock)
    static void fire(const std::vector<SubPtr>& subs, const DirEvent& ev) {
        for (auto& sub : subs) sub->first(ev);
    }

    // Recursively propagate subscriber to all sub-directories (write-locked by caller)
    static void propagate_subscriber(
        DirectoryInternal& internal,
        std::uint64_t id,
        std::shared_ptr<std::pair<std::function<void(const DirEvent&)>, utils::Mutex<std::deque<DirEvent>>>> sub);

    // Recursively remove subscriber from all sub-directories (write-locked by caller)
    static void remove_subscriber(DirectoryInternal& internal, std::uint64_t id);

    // Returns {parent_ptr, last_component} or error.
    std::expected<std::pair<std::shared_ptr<utils::RwLock<DirectoryInternal>>, std::string>, DirectoryError>
    resolve_parent(const std::filesystem::path& norm) const;

    // Same as resolve_parent but also navigates the last component as a directory
    std::expected<std::shared_ptr<utils::RwLock<DirectoryInternal>>, DirectoryError> resolve_dir(
        const std::filesystem::path& norm) const;
};

}  // namespace memory
}  // namespace epix::assets
