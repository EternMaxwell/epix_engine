module;

export module epix.assets:io.file.asset;

import std;
import epix.meta;
import epix.utils;

import :io.reader;

namespace epix::assets {
export struct FileAssetReader : public AssetReader {
   private:
    std::filesystem::path m_root;

   public:
    FileAssetReader(std::filesystem::path root) : m_root(std::filesystem::absolute(std::move(root))) {}
    std::expected<std::unique_ptr<std::istream>, AssetReaderError> read(
        const std::filesystem::path& path) const override {
        try {
            auto full_path = m_root / path;
            if (!std::filesystem::exists(full_path) || !std::filesystem::is_regular_file(full_path)) {
                return std::unexpected(AssetReaderError(reader_errors::NotFound{full_path}));
            }
            auto stream = std::make_unique<std::ifstream>(full_path, std::ios::binary);
            if (!stream->is_open()) {
                return std::unexpected(
                    AssetReaderError(reader_errors::IoError{std::make_error_code(std::io_errc::stream)}));
            }
            return std::expected<std::unique_ptr<std::istream>, AssetReaderError>(std::move(stream));
        } catch (const std::system_error& e) {
            return std::unexpected(AssetReaderError(reader_errors::IoError{e.code()}));
        } catch (...) {
            return std::unexpected(AssetReaderError(std::current_exception()));
        }
    }
    std::expected<std::unique_ptr<std::istream>, AssetReaderError> read_meta(
        const std::filesystem::path& path) const override {
        return read(get_meta_path(path));
    }
    std::expected<utils::input_iterable<std::filesystem::path>, AssetReaderError> read_directory(
        const std::filesystem::path& path) const override {
        try {
            auto full_path = m_root / path;
            if (!std::filesystem::exists(full_path) || !std::filesystem::is_directory(full_path)) {
                return std::unexpected(AssetReaderError(reader_errors::NotFound{full_path}));
            }
            return std::expected<utils::input_iterable<std::filesystem::path>, AssetReaderError>(
                std::filesystem::directory_iterator(full_path) |
                std::views::transform(
                    [rel = path](const std::filesystem::directory_entry& e) { return rel / e.path().filename(); }));
        } catch (const std::system_error& e) {
            return std::unexpected(AssetReaderError(reader_errors::IoError{e.code()}));
        } catch (...) {
            return std::unexpected(AssetReaderError(std::current_exception()));
        }
    }
    std::expected<bool, AssetReaderError> is_directory(const std::filesystem::path& path) const override {
        try {
            auto full_path = m_root / path;
            if (!std::filesystem::exists(full_path)) {
                return std::unexpected(AssetReaderError(reader_errors::NotFound{full_path}));
            }
            return std::expected<bool, AssetReaderError>(std::filesystem::is_directory(full_path));
        } catch (const std::system_error& e) {
            return std::unexpected(AssetReaderError(reader_errors::IoError{e.code()}));
        } catch (...) {
            return std::unexpected(AssetReaderError(std::current_exception()));
        }
    }
};
static_assert(!std::is_abstract_v<FileAssetReader>);

export struct FileAssetWriter : public AssetWriter {
   private:
    std::filesystem::path m_root;

   public:
    FileAssetWriter(std::filesystem::path root) : m_root(std::filesystem::absolute(std::move(root))) {}
    std::expected<std::unique_ptr<std::ostream>, AssetWriterError> write(
        const std::filesystem::path& path) const override {
        try {
            auto full_path = m_root / path;
            std::filesystem::create_directories(full_path.parent_path());
            auto stream = std::make_unique<std::ofstream>(full_path, std::ios::binary);
            if (!stream->is_open()) {
                return std::unexpected(
                    AssetWriterError(writer_errors::IoError{std::make_error_code(std::io_errc::stream)}));
            }
            return std::expected<std::unique_ptr<std::ostream>, AssetWriterError>(std::move(stream));
        } catch (const std::system_error& e) {
            return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
        } catch (...) {
            return std::unexpected(AssetWriterError(std::current_exception()));
        }
    }
    std::expected<std::unique_ptr<std::ostream>, AssetWriterError> write_meta(
        const std::filesystem::path& path) const override {
        return write(get_meta_path(path));
    }
    std::expected<void, AssetWriterError> remove(const std::filesystem::path& path) const override {
        try {
            auto full_path = m_root / path;
            if (std::filesystem::exists(full_path) && std::filesystem::is_regular_file(full_path)) {
                std::filesystem::remove(full_path);
            }
            return {};
        } catch (const std::system_error& e) {
            return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
        } catch (...) {
            return std::unexpected(AssetWriterError(std::current_exception()));
        }
    }
    std::expected<void, AssetWriterError> remove_meta(const std::filesystem::path& path) const override {
        return remove(get_meta_path(path));
    }
    std::expected<void, AssetWriterError> rename(const std::filesystem::path& old_path,
                                                 const std::filesystem::path& new_path) const override {
        try {
            auto full_old_path = m_root / old_path;
            auto full_new_path = m_root / new_path;
            if (std::filesystem::exists(full_old_path)) {
                std::filesystem::create_directories(full_new_path.parent_path());
                std::filesystem::rename(full_old_path, full_new_path);
            }
            return {};
        } catch (const std::system_error& e) {
            return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
        } catch (...) {
            return std::unexpected(AssetWriterError(std::current_exception()));
        }
    }
    std::expected<void, AssetWriterError> rename_meta(const std::filesystem::path& old_path,
                                                      const std::filesystem::path& new_path) const override {
        return rename(get_meta_path(old_path), get_meta_path(new_path));
    }
    std::expected<void, AssetWriterError> create_directory(const std::filesystem::path& path) const override {
        try {
            auto full_path = m_root / path;
            std::filesystem::create_directories(full_path);
            return {};
        } catch (const std::system_error& e) {
            return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
        } catch (...) {
            return std::unexpected(AssetWriterError(std::current_exception()));
        }
    }
    std::expected<void, AssetWriterError> remove_directory(const std::filesystem::path& path) const override {
        try {
            auto full_path = m_root / path;
            if (std::filesystem::exists(full_path) && std::filesystem::is_directory(full_path)) {
                std::filesystem::remove_all(full_path);
            }
            return {};
        } catch (const std::system_error& e) {
            return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
        } catch (...) {
            return std::unexpected(AssetWriterError(std::current_exception()));
        }
    }
    std::expected<void, AssetWriterError> clear_directory(const std::filesystem::path& path) const override {
        try {
            auto full_path = m_root / path;
            if (std::filesystem::exists(full_path) && std::filesystem::is_directory(full_path)) {
                for (const auto& entry : std::filesystem::directory_iterator(full_path)) {
                    std::filesystem::remove_all(entry.path());
                }
            }
            return {};
        } catch (const std::system_error& e) {
            return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
        } catch (...) {
            return std::unexpected(AssetWriterError(std::current_exception()));
        }
    }
};
static_assert(!std::is_abstract_v<FileAssetWriter>);
}  // namespace epix::assets