module;
module epix.assets;

import std;

namespace epix::assets {

std::expected<std::vector<std::byte>, AssetReaderError> AssetReader::read_meta_bytes(
    const std::filesystem::path& path) const {
    return read_meta(path).and_then(
        [](std::unique_ptr<std::istream>&& stream) -> std::expected<std::vector<std::byte>, AssetReaderError> {
            try {
                auto bytes =
                    std::ranges::subrange(std::istreambuf_iterator<char>(*stream), std::istreambuf_iterator<char>()) |
                    std::views::transform([](char c) { return static_cast<std::byte>(c); }) |
                    std::ranges::to<std::vector<std::byte>>();
                return std::expected<std::vector<std::byte>, AssetReaderError>(std::move(bytes));
            } catch (const std::ios_base::failure& e) {
                return std::unexpected(AssetReaderError(reader_errors::IoError{e.code()}));
            } catch (...) {
                return std::unexpected(AssetReaderError(std::current_exception()));
            }
        });
}

std::expected<void, AssetWriterError> AssetWriter::write_bytes(const std::filesystem::path& path,
                                                               std::span<const std::byte> bytes) const {
    return write(path).and_then(
        [&bytes](std::unique_ptr<std::ostream>&& stream) -> std::expected<void, AssetWriterError> {
            try {
                stream->write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                if (!*stream) {
                    return std::unexpected(AssetWriterError(writer_errors::IoError{
                        std::error_code(static_cast<int>(stream->rdstate()), std::iostream_category())}));
                }
                return {};
            } catch (const std::ios_base::failure& e) {
                return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
            } catch (...) {
                return std::unexpected(AssetWriterError(std::current_exception()));
            }
        });
}

std::expected<void, AssetWriterError> AssetWriter::write_meta_bytes(const std::filesystem::path& path,
                                                                    std::span<const std::byte> bytes) const {
    return write_meta(path).and_then(
        [&bytes](std::unique_ptr<std::ostream>&& stream) -> std::expected<void, AssetWriterError> {
            try {
                stream->write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                if (!*stream) {
                    return std::unexpected(AssetWriterError(writer_errors::IoError{
                        std::error_code(static_cast<int>(stream->rdstate()), std::iostream_category())}));
                }
                return {};
            } catch (const std::ios_base::failure& e) {
                return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
            } catch (...) {
                return std::unexpected(AssetWriterError(std::current_exception()));
            }
        });
}

}  // namespace epix::assets
