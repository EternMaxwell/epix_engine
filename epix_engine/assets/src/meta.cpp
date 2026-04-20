module;

#include <spdlog/spdlog.h>
#include <zpp_bits.h>

#include <asio/awaitable.hpp>

module epix.assets;

import std;

namespace epix::assets {

// ---------------------------------------------------------------------------
// AssetHasher (internal — not exported)
// ---------------------------------------------------------------------------

struct AssetHasher {
    uint64_t s[4] = {
        0xcbf29ce484222325ULL,  // FNV-1a 64-bit offset basis
        0x9e3779b97f4a7c15ULL,  // golden ratio x 2^64
        0x6c62272e07bb0142ULL,  // random constant
        0x14020a57acced8b7ULL,  // random constant
    };

    void update(const uint8_t* data, std::size_t len) noexcept {
        for (std::size_t i = 0; i < len; ++i) {
            const uint64_t b = data[i];
            s[0]             = (s[0] ^ b) * 0x00000100000001B3ULL;
            s[1]             = (s[1] + b) * 0x9e3779b97f4a7415ULL;
            s[2] ^= (s[0] >> 17) | (s[0] << 47);
            s[3] ^= (s[1] >> 23) | (s[1] << 41);
        }
    }

    void update(std::span<const std::byte> data) noexcept {
        update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }

    AssetHash finish() noexcept {
        for (auto& x : s) {
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdULL;
            x ^= x >> 33;
            x *= 0xc4ceb9fe1a85ec53ULL;
            x ^= x >> 33;
        }
        AssetHash result;
        std::memcpy(result.data(), s, 32);
        return result;
    }
};

// ---------------------------------------------------------------------------
// serialize / deserialize helpers
// ---------------------------------------------------------------------------

std::expected<std::vector<std::byte>, std::errc> serialize_meta_minimal(const AssetMetaMinimal& minimal) {
    std::vector<std::byte> data;
    zpp::bits::out out{data};
    auto result = out(minimal);
    if (zpp::bits::failure(result)) return std::unexpected(result);
    return data;
}

std::expected<AssetMetaMinimal, std::errc> deserialize_meta_minimal(std::span<const std::byte> bytes) {
    AssetMetaMinimal minimal;
    zpp::bits::in in(bytes);
    auto result = in(minimal);
    if (zpp::bits::failure(result)) return std::unexpected(result);
    return minimal;
}

std::expected<std::optional<ProcessedInfo>, std::errc> deserialize_processed_info(std::span<const std::byte> bytes) {
    std::string meta_format_version;
    std::optional<ProcessedInfo> info;
    zpp::bits::in in(bytes);
    auto result = in(meta_format_version, info);
    if (zpp::bits::failure(result)) return std::unexpected(result);
    return info;
}

// ---------------------------------------------------------------------------
// Asset hashing utilities
// ---------------------------------------------------------------------------

asio::awaitable<AssetHash> get_asset_hash(std::span<const std::byte> meta_bytes, Reader& reader) {
    AssetHasher hasher;
    hasher.update(meta_bytes);
    std::vector<uint8_t> bytes;
    co_await reader.read_to_end(bytes);
    hasher.update(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
    co_return hasher.finish();
}

AssetHash get_full_asset_hash(AssetHash asset_hash, const std::vector<AssetHash>& dependency_hashes) {
    AssetHasher hasher;
    hasher.update(std::span<const std::byte>(reinterpret_cast<const std::byte*>(asset_hash.data()), asset_hash.size()));
    for (const auto& dep : dependency_hashes) {
        hasher.update(std::span<const std::byte>(reinterpret_cast<const std::byte*>(dep.data()), dep.size()));
    }
    return hasher.finish();
}

}  // namespace epix::assets
