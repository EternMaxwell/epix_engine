export module epix.assets:async_channel;

import epix.async_channel;

// Re-export async_channel into assets::async_channel namespace alias
// so existing assets code using async_channel:: still works.
namespace epix::assets::async_channel {
using namespace epix::async_channel;
}
