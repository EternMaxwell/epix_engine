module;

export module epix.assets:io.memory.asset;

import std;
import epix.meta;
import epix.utils;

import :io.reader;
import :io.memory;

namespace assets {

// helper: convert DirectoryError variant to AssetReaderError
static AssetReaderError dir_error_to_reader_error(const memory::DirectoryError& derr,
                                                  const std::filesystem::path& /*fallback_path*/ = {}) {
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

// helper: convert DirectoryError variant to AssetWriterError
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

// Implement polymorphic adapters that satisfy the engine AssetReader/AssetWriter/AssetWatcher
export struct MemoryAssetReader : public assets::AssetReader {
   public:
    explicit MemoryAssetReader(assets::memory::Directory dir) : dir_(std::move(dir)) {}

    std::expected<std::unique_ptr<std::istream>, assets::AssetReaderError> read(
        const std::filesystem::path& path) const override {
        auto res = dir_.get_file(path);
        if (!res.has_value()) return std::unexpected(dir_error_to_reader_error(res.error(), path));
        auto data = res.value();
        // extract bytes
        if (std::holds_alternative<std::shared_ptr<std::vector<std::uint8_t>>>(data.value.v)) {
            auto buf = std::get<std::shared_ptr<std::vector<std::uint8_t>>>(data.value.v);
            std::string s(buf->begin(), buf->end());
            auto stream = std::make_unique<std::istringstream>(s, std::ios::binary);
            return std::expected<std::unique_ptr<std::istream>, assets::AssetReaderError>(std::move(stream));
        } else if (std::holds_alternative<std::span<const std::uint8_t>>(data.value.v)) {
            auto sp = std::get<std::span<const std::uint8_t>>(data.value.v);
            std::string s(reinterpret_cast<const char*>(sp.data()), sp.size());
            auto stream = std::make_unique<std::istringstream>(s, std::ios::binary);
            return std::expected<std::unique_ptr<std::istream>, assets::AssetReaderError>(std::move(stream));
        }
        return std::unexpected(assets::AssetReaderError(std::current_exception()));
    }

    std::expected<std::unique_ptr<std::istream>, assets::AssetReaderError> read_meta(
        const std::filesystem::path& path) const override {
        return read(assets::get_meta_path(path));
    }

    std::expected<utils::input_iterable<std::filesystem::path>, assets::AssetReaderError> read_directory(
        const std::filesystem::path& path) const override {
        auto res = dir_.list_directory(path, false);
        if (!res.has_value()) return std::unexpected(dir_error_to_reader_error(res.error(), path));
        return res.value();
    }

    std::expected<bool, assets::AssetReaderError> is_directory(const std::filesystem::path& path) const override {
        auto res = dir_.is_directory(path);
        if (!res.has_value()) return std::unexpected(dir_error_to_reader_error(res.error(), path));
        return res.value();
    }

   private:
    assets::memory::Directory dir_;
};

export struct MemoryAssetWriter : public assets::AssetWriter {
   public:
    explicit MemoryAssetWriter(assets::memory::Directory dir) : dir_(std::move(dir)) {}

    std::expected<std::unique_ptr<std::ostream>, assets::AssetWriterError> write(
        const std::filesystem::path& path) const override {
        // create an ostringstream that commits on destruction
        struct CommitOStream : std::ostringstream {
            assets::memory::Directory dir;
            std::filesystem::path path;
            CommitOStream(assets::memory::Directory d, std::filesystem::path p)
                : dir(std::move(d)), path(std::move(p)) {}
            ~CommitOStream() noexcept {
                try {
                    auto s   = this->str();
                    auto val = assets::memory::Value::from_shared(
                        std::make_shared<std::vector<std::uint8_t>>(s.begin(), s.end()));
                    auto res = dir.insert_file(path, std::move(val));
                } catch (...) {}
            }
        };

        auto stream = std::make_unique<CommitOStream>(dir_, path);
        return std::expected<std::unique_ptr<std::ostream>, assets::AssetWriterError>(std::move(stream));
    }

    std::expected<std::unique_ptr<std::ostream>, assets::AssetWriterError> write_meta(
        const std::filesystem::path& path) const override {
        return write(assets::get_meta_path(path));
    }

    std::expected<void, assets::AssetWriterError> remove(const std::filesystem::path& path) const override {
        auto res = dir_.remove_file(path);
        if (!res.has_value()) return std::unexpected(dir_error_to_writer_error(res.error()));
        return {};
    }

    std::expected<void, assets::AssetWriterError> remove_meta(const std::filesystem::path& path) const override {
        return remove(assets::get_meta_path(path));
    }

    std::expected<void, assets::AssetWriterError> rename(const std::filesystem::path& old_path,
                                                         const std::filesystem::path& new_path) const override {
        auto res = dir_.move(old_path, new_path);
        if (!res.has_value()) return std::unexpected(dir_error_to_writer_error(res.error()));
        return {};
    }

    std::expected<void, assets::AssetWriterError> rename_meta(const std::filesystem::path& old_path,
                                                              const std::filesystem::path& new_path) const override {
        return rename(assets::get_meta_path(old_path), assets::get_meta_path(new_path));
    }

    std::expected<void, assets::AssetWriterError> create_directory(const std::filesystem::path& path) const override {
        auto res = dir_.create_directory(path);
        if (!res.has_value()) return std::unexpected(dir_error_to_writer_error(res.error()));
        return {};
    }

    std::expected<void, assets::AssetWriterError> remove_directory(const std::filesystem::path& path) const override {
        auto res = dir_.remove_directory(path);
        if (!res.has_value()) return std::unexpected(dir_error_to_writer_error(res.error()));
        return {};
    }

    std::expected<void, assets::AssetWriterError> clear_directory(const std::filesystem::path& path) const override {
        // clear contents without removing the directory node itself to avoid DirRemoved/DirAdded on the parent
        auto list_res = dir_.list_directory(path, false);
        if (!list_res.has_value()) return std::unexpected(dir_error_to_writer_error(list_res.error()));

        // helper: recursive remove
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
            if (!is_dir.has_value()) return std::unexpected(dir_error_to_writer_error(is_dir.error()));
            if (is_dir.value()) {
                auto r = remove_recursive(child);
                if (!r.has_value()) return r;
            } else {
                auto r = dir_.remove_file(child);
                if (!r.has_value()) return std::unexpected(dir_error_to_writer_error(r.error()));
            }
        }
        return {};
    }

   private:
    assets::memory::Directory dir_;
};

export struct MemoryAssetWatcher : public assets::AssetWatcher {
   public:
    // callback receives AssetSourceEvent
    MemoryAssetWatcher(assets::memory::Directory dir, std::function<void(assets::AssetSourceEvent)> cb)
        : dir_(std::move(dir)), cb_(std::move(cb)) {
        sub_id_ = dir_.add_callback([this](const assets::memory::DirEvent& ev) {
            try {
                auto evt = convert(ev);
                cb_(std::move(evt));
            } catch (...) {}
        });
    }
    ~MemoryAssetWatcher() { dir_.remove_callback(sub_id_); }

   private:
    assets::AssetSourceEvent convert(const assets::memory::DirEvent& ev) {
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

    assets::memory::Directory dir_;
    std::function<void(assets::AssetSourceEvent)> cb_;
    std::uint64_t sub_id_ = 0;
};

}  // namespace assets