#include <asio/detail/config.hpp>
#include <stdexec/execution.hpp>

#ifdef ASIO_HAS_FILE
#include <asio/buffer.hpp>
#include <asio/read.hpp>
#include <asio/stream_file.hpp>
#include <asio/write.hpp>
#include <exec/asio/use_sender.hpp>

#endif  // ASIO_HAS_FILE
// Linux: io_uring headers available at build time; library loaded at runtime via dlopen.
// ASIO_HAS_FILE is NOT set on Linux (we do not use ASIO's io_uring integration).
#if defined(EPIX_HAS_URING_HEADERS) && !defined(ASIO_HAS_FILE)
#include <asio/posix/stream_descriptor.hpp>
#include <exec/asio/use_sender.hpp>
#include <coroutine>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <liburing.h>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

#include <epix/assets.hpp>
#include <epix/task.hpp>
#include <epix/utils.hpp>

namespace epix::assets {

#ifdef ASIO_HAS_FILE

// ---- io_uring path: true async file I/O, zero intermediate copies ----

using FileStream = asio::basic_stream_file<task::AsioExecutor>;

struct FileReader : Reader {
    FileStream m_file;

    explicit FileReader(FileStream file) : m_file(std::move(file)) {}

    STDEXEC::task<std::expected<size_t, std::error_code>> read_to_end(std::vector<uint8_t>& buf) override {
        const auto initial_size = buf.size();
        try {
            co_await asio::async_read(m_file, asio::dynamic_buffer(buf), exec::asio::use_sender);
        } catch (const std::system_error& e) {
            if (e.code() != asio::error::eof) {
                co_return std::unexpected(e.code());
            }
        }
        co_return buf.size() - initial_size;
    }
};

struct FileWriter : Writer {
    FileStream m_file;

    explicit FileWriter(FileStream file) : m_file(std::move(file)) {}

    STDEXEC::task<std::expected<size_t, std::error_code>> write(std::span<const uint8_t> data) override {
        try {
            std::size_t n =
                co_await asio::async_write(m_file, asio::buffer(data.data(), data.size()), exec::asio::use_sender);
            co_return n;
        } catch (const std::system_error& e) {
            co_return std::unexpected(e.code());
        }
    }

    STDEXEC::task<std::expected<void, std::error_code>> flush() override {
        // stream_file bypasses userspace buffering; writes go directly to the OS.
        co_return std::expected<void, std::error_code>{};
    }
};

#elif defined(EPIX_HAS_URING_HEADERS)

// ---- io_uring path: dlopen-based lazy loading of liburing -------------------
// liburing.so is opened at runtime; if unavailable, pread/pwrite are used as
// the synchronous fallback.  No hard link to liburing - binary runs on any
// Linux machine regardless of whether liburing is installed.

struct UringApi {
    using fn_queue_init = int (*)(unsigned, struct io_uring*, unsigned);
    using fn_queue_exit = void (*)(struct io_uring*);
    using fn_submit     = int (*)(struct io_uring*);
    using fn_register_eventfd = int (*)(struct io_uring*, int);

    fn_queue_init queue_init             = nullptr;
    fn_queue_exit queue_exit             = nullptr;
    fn_submit submit                     = nullptr;
    fn_register_eventfd register_eventfd = nullptr;
    bool available                       = false;

    static const UringApi& get() noexcept {
        static const UringApi inst = load();
        return inst;
    }

   private:
    static UringApi load() noexcept {
        UringApi api;
        void* h = ::dlopen("liburing.so.2", RTLD_LAZY);
        if (!h) h = ::dlopen("liburing.so", RTLD_LAZY);
        if (!h) return api;
        auto sym        = [h](const char* n) noexcept { return ::dlsym(h, n); };
        api.queue_init  = reinterpret_cast<fn_queue_init>(sym("io_uring_queue_init"));
        api.queue_exit  = reinterpret_cast<fn_queue_exit>(sym("io_uring_queue_exit"));
        api.submit      = reinterpret_cast<fn_submit>(sym("io_uring_submit"));
        api.register_eventfd = reinterpret_cast<fn_register_eventfd>(sym("io_uring_register_eventfd"));
        api.available   = api.queue_init && api.queue_exit && api.submit && api.register_eventfd;
        return api;
    }
};

struct UringService;

struct UringOperation {
    std::shared_ptr<UringService> service;
    int fd = -1;
    void* data = nullptr;
    std::size_t size = 0;
    std::uint64_t offset = 0;
    bool write = false;
    bool prepared = false;
    int result = -EINTR;
    std::coroutine_handle<> continuation;

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept;
    int await_resume() const noexcept { return result; }
};

// This follows Asio's io_uring service: one shared ring is driven by an
// eventfd readiness notification, while the task pool supplies the reactor.
struct UringService : std::enable_shared_from_this<UringService> {
    const UringApi& api;
    io_uring ring{};
    std::mutex mutex;
    std::deque<UringOperation*> pending;
    std::optional<asio::posix::stream_descriptor> event;
    int event_fd = -1;
    bool ring_initialized = false;
    bool ready = false;

    explicit UringService(const UringApi& uring_api) : api(uring_api) {
        if (!api.available || api.queue_init(1024, &ring, 0) < 0) return;
        ring_initialized = true;

        event_fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (event_fd < 0 || api.register_eventfd(&ring, event_fd) < 0) {
            if (event_fd >= 0) ::close(event_fd);
            event_fd = -1;
            api.queue_exit(&ring);
            ring_initialized = false;
            return;
        }

        try {
            event.emplace(task::IoTaskPool::get().get_asio_executor(), event_fd);
        } catch (...) {
            ::close(event_fd);
            event_fd = -1;
            api.queue_exit(&ring);
            ring_initialized = false;
            return;
        }
        ready = true;
    }

    ~UringService() {
        if (event) {
            event.reset();
            event_fd = -1;
        }
        if (event_fd >= 0) ::close(event_fd);
        if (ring_initialized) api.queue_exit(&ring);
    }

    static std::shared_ptr<UringService> get() noexcept {
        static std::mutex service_mutex;
        static std::weak_ptr<UringService> service;
        std::lock_guard lock(service_mutex);

        if (auto current = service.lock()) return current;
        try {
            auto current = std::make_shared<UringService>(UringApi::get());
            if (!current->ready) return {};
            service = current;
            current->start();
            return current;
        } catch (...) {
            return {};
        }
    }

    void start() {
        auto current = shared_from_this();
        task::IoTaskPool::get()
            .spawn([](std::shared_ptr<UringService> service) -> STDEXEC::task<void> {
                co_await service->run();
            }(std::move(current)))
            .detach();
    }

    STDEXEC::task<void> run() {
        while (ready) {
            try {
                co_await event->async_wait(asio::posix::stream_descriptor::wait_read,
                                           exec::asio::use_sender);
            } catch (...) {
                co_return;
            }

            std::uint64_t counter = 0;
            while (::read(event_fd, &counter, sizeof(counter)) < 0 && errno == EINTR) {}
            drain();
        }
    }

    void submit(UringOperation* operation) noexcept {
        std::lock_guard lock(mutex);
        pending.push_back(operation);
        submit_pending();
    }

    void submit_pending() noexcept {
        while (!pending.empty()) {
            auto* operation = pending.front();
            if (!operation->prepared) {
                auto* sqe = io_uring_get_sqe(&ring);
                if (!sqe) {
                    const int submitted = api.submit(&ring);
                    if (submitted < 0) return;
                    sqe = io_uring_get_sqe(&ring);
                    if (!sqe) return;
                }

                if (operation->write) {
                    io_uring_prep_write(sqe, operation->fd, operation->data,
                                        static_cast<unsigned>(operation->size), operation->offset);
                } else {
                    io_uring_prep_read(sqe, operation->fd, operation->data,
                                       static_cast<unsigned>(operation->size), operation->offset);
                }
                io_uring_sqe_set_data(sqe, operation);
                operation->prepared = true;
            }

            int submitted = api.submit(&ring);
            while (submitted == -EINTR) submitted = api.submit(&ring);
            if (submitted <= 0) return;
            pending.pop_front();
        }
    }

    void drain() noexcept {
        std::vector<UringOperation*> completed;
        {
            std::lock_guard lock(mutex);
            io_uring_cqe* cqe = nullptr;
            while (io_uring_peek_cqe(&ring, &cqe) == 0) {
                if (auto* operation = static_cast<UringOperation*>(io_uring_cqe_get_data(cqe))) {
                    operation->result = cqe->res;
                    completed.push_back(operation);
                }
                io_uring_cqe_seen(&ring, cqe);
                cqe = nullptr;
            }
            submit_pending();
        }

        for (auto* operation : completed) operation->continuation.resume();
    }
};

void UringOperation::await_suspend(std::coroutine_handle<> handle) noexcept {
    continuation = handle;
    service->submit(this);
}

struct FileReader : Reader {
    int m_fd;
    std::size_t m_file_size;

    FileReader(int fd, std::size_t file_size) : m_fd(fd), m_file_size(file_size) {}
    ~FileReader() {
        if (m_fd >= 0) ::close(m_fd);
    }

    STDEXEC::task<std::expected<size_t, std::error_code>> read_to_end(std::vector<uint8_t>& buf) override {
        const auto offset = buf.size();
        buf.resize(offset + m_file_size);

        if (m_file_size > 0) {
            if (auto service = UringService::get()) {
                const int res = co_await UringOperation{std::move(service), m_fd, buf.data() + offset,
                                                        m_file_size, 0, false};
                if (res < 0) {
                    buf.resize(offset);
                    co_return std::unexpected(std::error_code(-res, std::system_category()));
                }
                const auto n = static_cast<std::size_t>(res);
                buf.resize(offset + n);
                co_return n;
            }
        }

        // Fallback: pread (liburing unavailable or ring/sqe init failed).
        const ssize_t n = ::pread(m_fd, buf.data() + offset, m_file_size, 0);
        if (n < 0) {
            buf.resize(offset);
            co_return std::unexpected(std::error_code(errno, std::system_category()));
        }
        const auto bytes = static_cast<std::size_t>(n);
        buf.resize(offset + bytes);
        co_return bytes;
    }
};

struct FileWriter : Writer {
    int m_fd;
    std::size_t m_pos = 0;

    explicit FileWriter(int fd) : m_fd(fd) {}
    ~FileWriter() {
        if (m_fd >= 0) ::close(m_fd);
    }

    STDEXEC::task<std::expected<size_t, std::error_code>> write(std::span<const uint8_t> data) override {
        if (!data.empty()) {
            if (auto service = UringService::get()) {
                const int res = co_await UringOperation{std::move(service), m_fd,
                                                        const_cast<uint8_t*>(data.data()), data.size(), m_pos, true};
                if (res < 0) {
                    co_return std::unexpected(std::error_code(-res, std::system_category()));
                }
                m_pos += static_cast<std::size_t>(res);
                co_return static_cast<std::size_t>(res);
            }
        }

        // Fallback: pwrite.
        const ssize_t res = ::pwrite(m_fd, data.data(), data.size(), m_pos);
        if (res < 0) {
            co_return std::unexpected(std::error_code(errno, std::system_category()));
        }
        m_pos += static_cast<std::size_t>(res);
        co_return static_cast<std::size_t>(res);
    }

    STDEXEC::task<std::expected<void, std::error_code>> flush() override {
        co_return std::expected<void, std::error_code>{};
    }
};

#else  // Synchronous fallback (no async or lazy-io_uring file I/O available)

// ---- Fallback: synchronous reads/writes directly into the caller's buffer ----
// Single copy (file - buf) for reads; no intermediate vector.

struct FileReader : Reader {
    std::ifstream m_stream;
    std::size_t m_file_size;

    explicit FileReader(std::ifstream stream, std::size_t file_size)
        : m_stream(std::move(stream)), m_file_size(file_size) {}

    STDEXEC::task<std::expected<size_t, std::error_code>> read_to_end(std::vector<uint8_t>& buf) override {
        const auto offset = buf.size();
        buf.resize(offset + m_file_size);
        if (!m_stream.read(reinterpret_cast<char*>(buf.data() + offset), static_cast<std::streamsize>(m_file_size))) {
            buf.resize(offset);
            co_return std::unexpected(std::make_error_code(std::io_errc::stream));
        }
        co_return m_file_size;
    }
};

struct FileWriter : Writer {
    std::ofstream m_stream;

    explicit FileWriter(std::ofstream stream) : m_stream(std::move(stream)) {}

    STDEXEC::task<std::expected<size_t, std::error_code>> write(std::span<const uint8_t> data) override {
        m_stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!m_stream) co_return std::unexpected(std::make_error_code(std::io_errc::stream));
        co_return data.size();
    }

    STDEXEC::task<std::expected<void, std::error_code>> flush() override {
        m_stream.flush();
        if (!m_stream) co_return std::unexpected(std::make_error_code(std::io_errc::stream));
        co_return std::expected<void, std::error_code>{};
    }
};

#endif  // ASIO_HAS_FILE

// ---- FileAssetReader ----------------------------------------------------

STDEXEC::task<std::expected<std::unique_ptr<Reader>, AssetReaderError>> FileAssetReader::read(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_regular_file(full_path)) {
            co_return std::unexpected(AssetReaderError(reader_errors::NotFound{full_path}));
        }
#ifdef ASIO_HAS_FILE
        auto executor = task::IoTaskPool::get().get_asio_executor();
        FileStream file(executor, full_path.string(), FileStream::read_only);
        co_return std::unique_ptr<Reader>(std::make_unique<FileReader>(std::move(file)));
#elif defined(EPIX_HAS_URING_HEADERS)
        int fd = ::open(full_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            co_return std::unexpected(
                AssetReaderError(reader_errors::IoError{std::error_code(errno, std::system_category())}));
        }
        struct stat st{};
        if (::fstat(fd, &st) < 0) {
            ::close(fd);
            co_return std::unexpected(
                AssetReaderError(reader_errors::IoError{std::error_code(errno, std::system_category())}));
        }
        co_return std::unique_ptr<Reader>(std::make_unique<FileReader>(fd, static_cast<std::size_t>(st.st_size)));
#else
        std::ifstream stream(full_path, std::ios::binary | std::ios::ate);
        if (!stream.is_open()) {
            co_return std::unexpected(
                AssetReaderError(reader_errors::IoError{std::make_error_code(std::io_errc::stream)}));
        }
        const auto file_size = static_cast<std::size_t>(stream.tellg());
        stream.seekg(0, std::ios::beg);
        co_return std::unique_ptr<Reader>(std::make_unique<FileReader>(std::move(stream), file_size));
#endif
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetReaderError(reader_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetReaderError(std::current_exception()));
    }
}

STDEXEC::task<std::expected<std::unique_ptr<Reader>, AssetReaderError>> FileAssetReader::read_meta(
    const std::filesystem::path& path) const {
    co_return co_await read(get_meta_path(path));
}

STDEXEC::task<std::expected<utils::input_iterable<std::filesystem::path>, AssetReaderError>>
FileAssetReader::read_directory(const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_directory(full_path)) {
            co_return std::unexpected(AssetReaderError(reader_errors::NotFound{full_path}));
        }
        co_return std::expected<utils::input_iterable<std::filesystem::path>, AssetReaderError>(
            std::filesystem::directory_iterator(full_path) |
            std::views::transform(
                [rel = path](const std::filesystem::directory_entry& e) { return rel / e.path().filename(); }));
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetReaderError(reader_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetReaderError(std::current_exception()));
    }
}

STDEXEC::task<std::expected<bool, AssetReaderError>> FileAssetReader::is_directory(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        if (!std::filesystem::exists(full_path)) {
            co_return std::unexpected(AssetReaderError(reader_errors::NotFound{full_path}));
        }
        co_return std::expected<bool, AssetReaderError>(std::filesystem::is_directory(full_path));
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetReaderError(reader_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetReaderError(std::current_exception()));
    }
}

// ---- FileAssetWriter ----------------------------------------------------

STDEXEC::task<std::expected<std::unique_ptr<Writer>, AssetWriterError>> FileAssetWriter::write(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        std::filesystem::create_directories(full_path.parent_path());
#ifdef ASIO_HAS_FILE
        auto executor = task::IoTaskPool::get().get_asio_executor();
        FileStream file(executor, full_path.string(),
                        FileStream::write_only | FileStream::create | FileStream::truncate);
        co_return std::unique_ptr<Writer>(std::make_unique<FileWriter>(std::move(file)));
#elif defined(EPIX_HAS_URING_HEADERS)
        int fd = ::open(full_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, static_cast<mode_t>(0644));
        if (fd < 0) {
            co_return std::unexpected(
                AssetWriterError(writer_errors::IoError{std::error_code(errno, std::system_category())}));
        }
        co_return std::unique_ptr<Writer>(std::make_unique<FileWriter>(fd));
#else
        std::ofstream stream(full_path, std::ios::binary);
        if (!stream.is_open()) {
            co_return std::unexpected(
                AssetWriterError(writer_errors::IoError{std::make_error_code(std::io_errc::stream)}));
        }
        co_return std::unique_ptr<Writer>(std::make_unique<FileWriter>(std::move(stream)));
#endif
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetWriterError(std::current_exception()));
    }
}

STDEXEC::task<std::expected<std::unique_ptr<Writer>, AssetWriterError>> FileAssetWriter::write_meta(
    const std::filesystem::path& path) const {
    co_return co_await write(get_meta_path(path));
}

STDEXEC::task<std::expected<void, AssetWriterError>> FileAssetWriter::remove(const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        if (std::filesystem::exists(full_path) && std::filesystem::is_regular_file(full_path)) {
            std::filesystem::remove(full_path);
        }
        co_return std::expected<void, AssetWriterError>{};
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetWriterError(std::current_exception()));
    }
}

STDEXEC::task<std::expected<void, AssetWriterError>> FileAssetWriter::remove_meta(
    const std::filesystem::path& path) const {
    co_return co_await remove(get_meta_path(path));
}

STDEXEC::task<std::expected<void, AssetWriterError>> FileAssetWriter::rename(
    const std::filesystem::path& old_path, const std::filesystem::path& new_path) const {
    try {
        auto full_old_path = m_root / old_path;
        auto full_new_path = m_root / new_path;
        if (std::filesystem::exists(full_old_path)) {
            std::filesystem::create_directories(full_new_path.parent_path());
            std::filesystem::rename(full_old_path, full_new_path);
        }
        co_return std::expected<void, AssetWriterError>{};
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetWriterError(std::current_exception()));
    }
}

STDEXEC::task<std::expected<void, AssetWriterError>> FileAssetWriter::rename_meta(
    const std::filesystem::path& old_path, const std::filesystem::path& new_path) const {
    co_return co_await rename(get_meta_path(old_path), get_meta_path(new_path));
}

STDEXEC::task<std::expected<void, AssetWriterError>> FileAssetWriter::create_directory(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        std::filesystem::create_directories(full_path);
        co_return std::expected<void, AssetWriterError>{};
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetWriterError(std::current_exception()));
    }
}

STDEXEC::task<std::expected<void, AssetWriterError>> FileAssetWriter::remove_directory(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        if (std::filesystem::exists(full_path) && std::filesystem::is_directory(full_path)) {
            std::filesystem::remove_all(full_path);
        }
        co_return std::expected<void, AssetWriterError>{};
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetWriterError(std::current_exception()));
    }
}

STDEXEC::task<std::expected<void, AssetWriterError>> FileAssetWriter::clear_directory(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        if (std::filesystem::exists(full_path) && std::filesystem::is_directory(full_path)) {
            for (const auto& entry : std::filesystem::directory_iterator(full_path)) {
                std::filesystem::remove_all(entry.path());
            }
        }
        co_return std::expected<void, AssetWriterError>{};
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetWriterError(std::current_exception()));
    }
}

}  // namespace epix::assets
