#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
#include <expected>
#include <span>
#include <stdexec/execution.hpp>
#include <string_view>
#endif

#include <epix/image/image.hpp>

namespace epix::image {

/** @brief Asset loader for formats supported by Epix's selected decoder. */
EPIX_EXPORT struct ImageLoader {
    using Asset = Image;
    struct Settings {};
    using Error = ImageLoadError;

    static std::span<std::string_view> extensions() noexcept;
    static STDEXEC::task<std::expected<Image, ImageLoadError>> load(assets::Reader& reader,
                                                                    const Settings& settings,
                                                                    assets::LoadContext& context);
};

}  // namespace epix::image
