module;
#include <epix/image.hpp>

export module epix.image;

export namespace epix::image {
using epix::image::Format;
using epix::image::FormatInfo;
using epix::image::Image;
using epix::image::ImageLoadError;
using epix::image::ImageLoader;
using epix::image::ImagePlugin;
using epix::image::ImageSampleError;
using epix::image::ImageSaveError;
using epix::image::ImageType;
using epix::image::ImageUsage;
using epix::image::ImageWriteError;
} // namespace epix::image
