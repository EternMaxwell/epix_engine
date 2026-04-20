module;

#include <asio/awaitable.hpp>

module epix.assets;

import std;
import epix.utils;

namespace epix::assets {

// ---- internal helpers ---------------------------------------------------

static AssetReaderError dir_error_to_reader_error(const memory::DirectoryError& derr,
                                                  const std::filesystem::path& /*fallback_path*/) {
    AssetReaderError are;
    std::visit(
        [&](auto&& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, memory::NotFoundError>) {
                are = reader_errors::NotFound{e.path};
            } else if constexpr (std::is_same_v<T, memory::IoError>) {
                are = reader_errors::IoError{e.code};
            } else {
                are = (e.cause ? e.cause : std::current_exception());
            }
        },
        derr);
    return are;
}

static AssetWriterError dir_error_to_writer_error(const memory::DirectoryError& derr) {
    AssetWriterError awe;
    std::visit(
        [&](auto&& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, memory::IoError>) {
                awe = writer_errors::IoError{e.code};
            } else if constexpr (std::is_same_v<T, memory::NotFoundError>) {
                awe = writer_errors::IoError{e.code};
            } else {
                awe = (e.cause ? e.cause : std::current_exception());
            }
        },
        derr);
    return awe;
}

// ---- MemoryAssetReader --------------------------------------------------

asio::awaitable<std::expected<std::unique_ptr<Reader>, assets::AssetReaderError>> MemoryAssetReader::read(
    const std::filesystem::path& path) const {
    auto res = dir_.get_file(path);
    if (!res.has_value()) co_return std::unexpected(dir_error_to_reader_error(res.error(), path));
    auto data = res.value();
    // extract bytes into a VecReader
    std::vector<uint8_t> bytes;
    if (std::holds_alternative<std::shared_ptr<std::vector<std::byte>>>(data.value.v)) {
        auto buf = std::get<std::shared_ptr<std::vector<std::byte>>>(data.value.v);
        bytes.resize(buf->size());
        std::memcpy(bytes.data(), buf->data(), buf->size());
    } else if (std::holds_alternative<std::span<const std::byte>>(data.value.v)) {
        auto sp = std::get<std::span<const std::byte>>(data.value.v);
        bytes.resize(sp.size());
        std::memcpy(bytes.data(), sp.data(), sp.size());
    } else {
        co_return std::unexpected(assets::AssetReaderError(std::current_exception()));
    }
    co_return std::unique_ptr<Reader>(std::make_unique<VecReader>(std::move(bytes)));
}

asio::awaitable<std::expected<std::unique_ptr<Reader>, assets::AssetReaderError>> MemoryAssetReader::read_meta(
    const std::filesystem::path& path) const {
    co_return co_await read(assets::get_meta_path(path));
}

asio::awaitable<std::expected<utils::input_iterable<std::filesystem::path>, assets::AssetReaderError>>
MemoryAssetReader::read_directory(const std::filesystem::path& path) const {
    auto res = dir_.list_directory(path, false);
    if (!res.has_value()) co_return std::unexpected(dir_error_to_reader_error(res.error(), path));
    co_return res.value();
}

asio::awaitable<std::expected<bool, assets::AssetReaderError>> MemoryAssetReader::is_directory(
    const std::filesystem::path& path) const {
    auto res = dir_.is_directory(path);
    if (!res.has_value()) co_return std::unexpected(dir_error_to_reader_error(res.error(), path));
    co_return res.value();
}

// ---- MemoryAssetWriter --------------------------------------------------

// A Writer that accumulates bytes in memory and commits to the Directory on destruction.
struct MemoryCommitWriter : assets::Writer {
    std::vector<uint8_t> m_data;
    assets::memory::Directory m_dir;
    std::filesystem::path m_path;

    MemoryCommitWriter(assets::memory::Directory dir, std::filesystem::path path)
        : m_dir(std::move(dir)), m_path(std::move(path)) {}

    ~MemoryCommitWriter() noexcept {
        try {
            auto sp = std::as_bytes(std::span(m_data));
            auto val =
                assets::memory::Value::from_shared(std::make_shared<std::vector<std::byte>>(sp.begin(), sp.end()));
            (void)m_dir.insert_file(m_path, std::move(val));
        } catch (...) {}
    }

    asio::awaitable<std::expected<size_t, std::error_code>> write(std::span<const uint8_t> data) override {
        m_data.insert(m_data.end(), data.begin(), data.end());
        co_return data.size();
    }

    asio::awaitable<std::expected<void, std::error_code>> flush() override {
        co_return std::expected<void, std::error_code>{};
    }
};

asio::awaitable<std::expected<std::unique_ptr<Writer>, assets::AssetWriterError>> MemoryAssetWriter::write(
    const std::filesystem::path& path) const {
    co_return std::unique_ptr<Writer>(std::make_unique<MemoryCommitWriter>(dir_, path));
}

asio::awaitable<std::expected<std::unique_ptr<Writer>, assets::AssetWriterError>> MemoryAssetWriter::write_meta(
    const std::filesystem::path& path) const {
    co_return co_await write(assets::get_meta_path(path));
}

asio::awaitable<std::expected<void, assets::AssetWriterError>> MemoryAssetWriter::remove(
    const std::filesystem::path& path) const {
    auto res = dir_.remove_file(path);
    if (!res.has_value()) co_return std::unexpected(dir_error_to_writer_error(res.error()));
    co_return std::expected<void, assets::AssetWriterError>{};
}

asio::awaitable<std::expected<void, assets::AssetWriterError>> MemoryAssetWriter::remove_meta(
    const std::filesystem::path& path) const {
    co_return co_await remove(assets::get_meta_path(path));
}

asio::awaitable<std::expected<void, assets::AssetWriterError>> MemoryAssetWriter::rename(
    const std::filesystem::path& old_path, const std::filesystem::path& new_path) const {
    auto res = dir_.move(old_path, new_path);
    if (!res.has_value()) co_return std::unexpected(dir_error_to_writer_error(res.error()));
    co_return std::expected<void, assets::AssetWriterError>{};
}

asio::awaitable<std::expected<void, assets::AssetWriterError>> MemoryAssetWriter::rename_meta(
    const std::filesystem::path& old_path, const std::filesystem::path& new_path) const {
    co_return co_await rename(assets::get_meta_path(old_path), assets::get_meta_path(new_path));
}

asio::awaitable<std::expected<void, assets::AssetWriterError>> MemoryAssetWriter::create_directory(
    const std::filesystem::path& path) const {
    auto res = dir_.create_directory(path);
    if (!res.has_value()) co_return std::unexpected(dir_error_to_writer_error(res.error()));
    co_return std::expected<void, assets::AssetWriterError>{};
}

asio::awaitable<std::expected<void, assets::AssetWriterError>> MemoryAssetWriter::remove_directory(
    const std::filesystem::path& path) const {
    auto res = dir_.remove_directory(path);
    if (!res.has_value()) co_return std::unexpected(dir_error_to_writer_error(res.error()));
    co_return std::expected<void, assets::AssetWriterError>{};
}

asio::awaitable<std::expected<void, assets::AssetWriterError>> MemoryAssetWriter::clear_directory(
    const std::filesystem::path& path) const {
    // clear contents without removing the directory node itself to avoid DirRemoved/DirAdded on the parent
    auto list_res = dir_.list_directory(path, false);
    if (!list_res.has_value()) co_return std::unexpected(dir_error_to_writer_error(list_res.error()));

    // helper: recursive remove (sync since memory::Directory ops are sync)
    std::function<std::expected<void, assets::AssetWriterError>(const std::filesystem::path&)> remove_recursive;
    remove_recursive = [&](const std::filesystem::path& p) -> std::expected<void, assets::AssetWriterError> {
        auto lr = dir_.list_directory(p, false);
        if (!lr.has_value()) return std::unexpected(dir_error_to_writer_error(lr.error()));
        for (auto&& child : lr.value()) {
            auto is_dir = dir_.is_directory(child);
            if (!is_dir.has_value()) return std::unexpected(dir_error_to_writer_error(is_dir.error()));
            if (is_dir.value()) {
                auto rr = remove_recursive(child);
                if (!rr.has_value()) return rr;
            } else {
                auto r = dir_.remove_file(child);
                if (!r.has_value()) return std::unexpected(dir_error_to_writer_error(r.error()));
            }
        }
        auto rem = dir_.remove_directory(p);
        if (!rem.has_value()) return std::unexpected(dir_error_to_writer_error(rem.error()));
        return {};
    };

    for (auto&& child : list_res.value()) {
        auto is_dir = dir_.is_directory(child);
        if (!is_dir.has_value()) co_return std::unexpected(dir_error_to_writer_error(is_dir.error()));
        if (is_dir.value()) {
            auto r = remove_recursive(child);
            if (!r.has_value()) co_return r;
        } else {
            auto r = dir_.remove_file(child);
            if (!r.has_value()) co_return std::unexpected(dir_error_to_writer_error(r.error()));
        }
    }
    co_return std::expected<void, assets::AssetWriterError>{};
}

// ---- MemoryAssetWatcher -------------------------------------------------

MemoryAssetWatcher::MemoryAssetWatcher(assets::memory::Directory dir, std::function<void(assets::AssetSourceEvent)> cb)
    : dir_(std::move(dir)), cb_(std::move(cb)) {
    sub_id_ = dir_.add_callback([this](const assets::memory::DirEvent& ev) {
        try {
            auto evt = convert(ev);
            cb_(std::move(evt));
        } catch (...) {}
    });
}

assets::AssetSourceEvent MemoryAssetWatcher::convert(const assets::memory::DirEvent& ev) {
    using namespace assets::source_events;
    bool is_meta = ev.path.extension() == ".meta";
    switch (ev.type) {
        case assets::memory::DirEventType::FileAdded: {
            if (is_meta) return AddedMeta{ev.path};
            return AddedAsset{ev.path};
        }
        case assets::memory::DirEventType::FileModified: {
            if (is_meta) return ModifiedMeta{ev.path};
            return ModifiedAsset{ev.path};
        }
        case assets::memory::DirEventType::FileRemoved: {
            if (is_meta) return RemovedMeta{ev.path};
            return RemovedAsset{ev.path};
        }
        case assets::memory::DirEventType::Moved: {
            if (!ev.old_path) return RemovedUnknown{ev.path, is_meta};
            auto is_dir_res = dir_.is_directory(ev.path);
            if (is_dir_res.has_value() && is_dir_res.value()) {
                return RenamedDirectory{*ev.old_path, ev.path};
            }
            bool new_is_meta = ev.path.extension() == ".meta";
            bool old_is_meta = ev.old_path->extension() == ".meta";
            if (new_is_meta || old_is_meta) {
                return RenamedMeta{*ev.old_path, ev.path};
            }
            return RenamedAsset{*ev.old_path, ev.path};
        }
        case assets::memory::DirEventType::DirAdded:
            return AddedDirectory{ev.path};
        case assets::memory::DirEventType::DirRemoved:
            return RemovedDirectory{ev.path};
        default:
            return RemovedUnknown{ev.path, is_meta};
    }
}

}  // namespace epix::assets
