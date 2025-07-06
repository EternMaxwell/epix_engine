#include "epix/window.h"

namespace epix::window {
EPIX_API void Window::request_attention(bool request) {
    attention_request = request;
}
}  // namespace epix::window