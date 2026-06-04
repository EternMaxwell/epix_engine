module;
#include <epix/async_broadcast.hpp>

export module epix.async_broadcast;

export namespace epix::async_broadcast {
using epix::async_broadcast::broadcast;
using epix::async_broadcast::InactiveReceiver;
using epix::async_broadcast::Receiver;
using epix::async_broadcast::RecvError;
using epix::async_broadcast::Sender;
using epix::async_broadcast::SendError;
using epix::async_broadcast::TryRecvError;
using epix::async_broadcast::TrySendError;
}  // namespace epix::async_broadcast
