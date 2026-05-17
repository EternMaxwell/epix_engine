module;

#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <ranges>
#include <span>
#include <system_error>
#include <utility>
#include <vector>
#endif
#include <asio/awaitable.hpp>
#include <asio/detail/config.hpp>
#ifdef ASIO_HAS_FILE
#include <asio/buffer.hpp>
#include <asio/read.hpp>
#include <asio/stream_file.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#endif  // ASIO_HAS_FILE
// Linux: io_uring headers available at build time; library loaded at runtime via dlopen.
// ASIO_HAS_FILE is NOT set on Linux (we do not use ASIO's io_uring integration).
#if defined(EPIX_HAS_URING_HEADERS) && !defined(ASIO_HAS_FILE)
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <liburing.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

module epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.utils;

namespace epix::assets {

#ifdef ASIO_HAS_FILE

// ---- io_uring path: true async file I/O, zero intermediate copies ----

struct FileReader : Reader {
    asio::stream_file m_file;

    explicit FileReader(asio::stream_file file) : m_file(std::move(file)) {}

    asio::awaitable<std::expected<size_t, std::error_code>> read_to_end(std::vector<uint8_t>& buf) override {
        const auto initial_size = buf.size();
        try {
            co_await asio::async_read(m_file, asio::dynamic_buffer(buf), asio::use_awaitable);
        } catch (const std::system_error& e) {
            if (e.code() != asio::error::eof) {
                co_return std::unexpected(e.code());
            }
        }
        co_return buf.size() - initial_size;
    }
};

struct FileWriter : Writer {
    asio::stream_file m_file;

    explicit FileWriter(asio::stream_file file) : m_file(std::move(file)) {}

    asio::awaitable<std::expected<size_t, std::error_code>> write(std::span<const uint8_t> data) override {
        try {
            std::size_t n =
                co_await asio::async_write(m_file, asio::buffer(data.data(), data.size()), asio::use_awaitable);
            co_return n;
        } catch (const std::system_error& e) {
            co_return std::unexpected(e.code());
        }
    }

    asio::awaitable<std::expected<void, std::error_code>> flush() override {
        // stream_file bypasses userspace buffering; writes go directly to the OS.
        co_return std::expected<void, std::error_code>{};
    }
};

#elif defined(EPIX_HAS_URING_HEADERS)

// ---- io_uring path: dlopen-based lazy loading of liburing -------------------
// liburing.so is opened at runtime; if unavailable, pread/pwrite are used as
// the synchronous fallback.  No hard link to liburing — binary runs on any
// Linux machine regardless of whether liburing is installed.

struct UringApi {
    using fn_queue_init = int (*)(unsigned, struct io_uring*, unsigned);
    using fn_queue_exit = void (*)(struct io_uring*);
    using fn_submit     = int (*)(struct io_uring*);
    // io_uring_wait_cqe is a static inline that calls this extern:
    using fn_wait_cqe_nr = int (*)(struct io_uring*, struct io_uring_cqe**, unsigned);

    fn_queue_init queue_init   = nullptr;
    fn_queue_exit queue_exit   = nullptr;
    fn_submit submit           = nullptr;
    fn_wait_cqe_nr wait_cqe_nr = nullptr;
    bool available             = false;

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
        api.wait_cqe_nr = reinterpret_cast<fn_wait_cqe_nr>(sym("io_uring_wait_cqe_nr"));
        api.available   = api.queue_init && api.queue_exit && api.submit && api.wait_cqe_nr;
        return api;
    }
};

struct FileReader : Reader {
    int m_fd;
    std::size_t m_file_size;

    FileReader(int fd, std::size_t file_size) : m_fd(fd), m_file_size(file_size) {}
    ~FileReader() {
        if (m_fd >= 0) ::close(m_fd);
    }

    asio::awaitable<std::expected<size_t, std::error_code>> read_to_end(std::vector<uint8_t>& buf) override {
        const auto& api   = UringApi::get();
        const auto offset = buf.size();
        buf.resize(offset + m_file_size);

        if (api.available && m_file_size > 0) {
            struct io_uring ring{};
            if (api.queue_init(8, &ring, 0) == 0) {
                auto* sqe = io_uring_get_sqe(&ring);
                if (sqe) {
                    io_uring_prep_read(sqe, m_fd, buf.data() + offset, static_cast<unsigned>(m_file_size), 0);
                    api.submit(&ring);
                    struct io_uring_cqe* cqe = nullptr;
                    const int wait_ret       = api.wait_cqe_nr(&ring, &cqe, 1);
                    const int res            = (wait_ret == 0 && cqe) ? cqe->res : -EINTR;
                    if (cqe) io_uring_cqe_seen(&ring, cqe);
                    api.queue_exit(&ring);

                    if (res < 0) {
                        buf.resize(offset);
                        co_return std::unexpected(std::error_code(-res, std::system_category()));
                    }
                    const auto n = static_cast<std::size_t>(res);
                    buf.resize(offset + n);
                    co_return n;
                }
                api.queue_exit(&ring);
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

    asio::awaitable<std::expected<size_t, std::error_code>> write(std::span<const uint8_t> data) override {
        const auto& api = UringApi::get();

        if (api.available && !data.empty()) {
            struct io_uring ring{};
            if (api.queue_init(8, &ring, 0) == 0) {
                auto* sqe = io_uring_get_sqe(&ring);
                if (sqe) {
                    io_uring_prep_write(sqe, m_fd, data.data(), static_cast<unsigned>(data.size()), m_pos);
                    api.submit(&ring);
                    struct io_uring_cqe* cqe = nullptr;
                    const int wait_ret       = api.wait_cqe_nr(&ring, &cqe, 1);
                    const int res            = (wait_ret == 0 && cqe) ? cqe->res : -EINTR;
                    if (cqe) io_uring_cqe_seen(&ring, cqe);
                    api.queue_exit(&ring);

                    if (res < 0) {
                        co_return std::unexpected(std::error_code(-res, std::system_category()));
                    }
                    m_pos += static_cast<std::size_t>(res);
                    co_return static_cast<std::size_t>(res);
                }
                api.queue_exit(&ring);
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

    asio::awaitable<std::expected<void, std::error_code>> flush() override {
        co_return std::expected<void, std::error_code>{};
    }
};

#else  // Synchronous fallback (no async or lazy-io_uring file I/O available)

// ---- Fallback: synchronous reads/writes directly into the caller's buffer ----
// Single copy (file → buf) for reads; no intermediate vector.

struct FileReader : Reader {
    std::ifstream m_stream;
    std::size_t m_file_size;

    explicit FileReader(std::ifstream stream, std::size_t file_size)
        : m_stream(std::move(stream)), m_file_size(file_size) {}

    asio::awaitable<std::expected<size_t, std::error_code>> read_to_end(std::vector<uint8_t>& buf) override {
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

    asio::awaitable<std::expected<size_t, std::error_code>> write(std::span<const uint8_t> data) override {
        m_stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!m_stream) co_return std::unexpected(std::make_error_code(std::io_errc::stream));
        co_return data.size();
    }

    asio::awaitable<std::expected<void, std::error_code>> flush() override {
        m_stream.flush();
        if (!m_stream) co_return std::unexpected(std::make_error_code(std::io_errc::stream));
        co_return std::expected<void, std::error_code>{};
    }
};

#endif  // ASIO_HAS_FILE

// ---- FileAssetReader ----------------------------------------------------

asio::awaitable<std::expected<std::unique_ptr<Reader>, AssetReaderError>> FileAssetReader::read(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_regular_file(full_path)) {
            co_return std::unexpected(AssetReaderError(reader_errors::NotFound{full_path}));
        }
#ifdef ASIO_HAS_FILE
        auto executor = co_await asio::this_coro::executor;
        asio::stream_file file(executor, full_path.string(), asio::stream_file::read_only);
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

asio::awaitable<std::expected<std::unique_ptr<Reader>, AssetReaderError>> FileAssetReader::read_meta(
    const std::filesystem::path& path) const {
    co_return co_await read(get_meta_path(path));
}

asio::awaitable<std::expected<utils::input_iterable<std::filesystem::path>, AssetReaderError>>
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

asio::awaitable<std::expected<bool, AssetReaderError>> FileAssetReader::is_directory(
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

asio::awaitable<std::expected<std::unique_ptr<Writer>, AssetWriterError>> FileAssetWriter::write(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        std::filesystem::create_directories(full_path.parent_path());
#ifdef ASIO_HAS_FILE
        auto executor = co_await asio::this_coro::executor;
        asio::stream_file file(executor, full_path.string(),
                               asio::stream_file::write_only | asio::stream_file::create | asio::stream_file::truncate);
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

asio::awaitable<std::expected<std::unique_ptr<Writer>, AssetWriterError>> FileAssetWriter::write_meta(
    const std::filesystem::path& path) const {
    co_return co_await write(get_meta_path(path));
}

asio::awaitable<std::expected<void, AssetWriterError>> FileAssetWriter::remove(
    const std::filesystem::path& path) const {
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

asio::awaitable<std::expected<void, AssetWriterError>> FileAssetWriter::remove_meta(
    const std::filesystem::path& path) const {
    co_return co_await remove(get_meta_path(path));
}

asio::awaitable<std::expected<void, AssetWriterError>> FileAssetWriter::rename(
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

asio::awaitable<std::expected<void, AssetWriterError>> FileAssetWriter::rename_meta(
    const std::filesystem::path& old_path, const std::filesystem::path& new_path) const {
    co_return co_await rename(get_meta_path(old_path), get_meta_path(new_path));
}

asio::awaitable<std::expected<void, AssetWriterError>> FileAssetWriter::create_directory(
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

asio::awaitable<std::expected<void, AssetWriterError>> FileAssetWriter::remove_directory(
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

asio::awaitable<std::expected<void, AssetWriterError>> FileAssetWriter::clear_directory(
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
