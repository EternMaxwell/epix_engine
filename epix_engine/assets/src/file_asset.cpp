module;
module epix.assets;

import std;
import epix.utils;

namespace epix::assets {

// ---- FileAssetReader ----------------------------------------------------

std::expected<std::unique_ptr<std::istream>, AssetReaderError> FileAssetReader::read(
    const std::filesystem::path& path) const {
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

std::expected<utils::input_iterable<std::filesystem::path>, AssetReaderError> FileAssetReader::read_directory(
    const std::filesystem::path& path) const {
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

std::expected<bool, AssetReaderError> FileAssetReader::is_directory(const std::filesystem::path& path) const {
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

// ---- FileAssetWriter ----------------------------------------------------

std::expected<std::unique_ptr<std::ostream>, AssetWriterError> FileAssetWriter::write(
    const std::filesystem::path& path) const {
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

std::expected<void, AssetWriterError> FileAssetWriter::remove(const std::filesystem::path& path) const {
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

std::expected<void, AssetWriterError> FileAssetWriter::rename(const std::filesystem::path& old_path,
                                                              const std::filesystem::path& new_path) const {
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

std::expected<void, AssetWriterError> FileAssetWriter::create_directory(const std::filesystem::path& path) const {
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

std::expected<void, AssetWriterError> FileAssetWriter::remove_directory(const std::filesystem::path& path) const {
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

std::expected<void, AssetWriterError> FileAssetWriter::clear_directory(const std::filesystem::path& path) const {
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

}  // namespace epix::assets
