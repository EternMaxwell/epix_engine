module;
#include <epix/time.hpp>

export module epix.time;

export namespace epix::time {
using epix::time::Fixed;
using epix::time::FixedFirst;
using epix::time::FixedFirstT;
using epix::time::FixedLast;
using epix::time::FixedLastT;
using epix::time::FixedMain;
using epix::time::FixedMainT;
using epix::time::FixedPostUpdate;
using epix::time::FixedPostUpdateT;
using epix::time::FixedPreUpdate;
using epix::time::FixedPreUpdateT;
using epix::time::FixedUpdate;
using epix::time::FixedUpdateT;
using epix::time::GenericTag;
using epix::time::Real;
using epix::time::Stopwatch;
using epix::time::Time;
using epix::time::TimePlugin;
using epix::time::Timer;
using epix::time::TimerMode;
using epix::time::TimeUpdateConfig;
using epix::time::TimeUpdateStrategy;
using epix::time::Virtual;
}  // namespace epix::time
