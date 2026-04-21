export module epix.assets:async_broadcast;

import epix.async_broadcast;

// Re-export async_broadcast into assets::async_broadcast namespace alias
// so existing assets code using async_broadcast:: still works.
namespace epix::assets::async_broadcast {
using namespace epix::async_broadcast;
}
