module;
#include <epix/async_channel.hpp>

export module epix.async_channel;

export namespace epix::async_channel {
using epix::async_channel::bounded;
using epix::async_channel::Receiver;
using epix::async_channel::RecvError;
using epix::async_channel::Sender;
using epix::async_channel::SendError;
using epix::async_channel::TryRecvError;
using epix::async_channel::TrySendError;
using epix::async_channel::unbounded;
using epix::async_channel::WeakReceiver;
using epix::async_channel::WeakSender;
}  // namespace epix::async_channel
