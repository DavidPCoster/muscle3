#include <libmuscle/profiler.hpp>

#include <libmuscle/profiling.hpp>


namespace libmuscle { namespace _MUSCLE_IMPL_NS {

Profiler::Profiler(MMPClient & manager)
    : manager_(manager)
    , enabled_(true)
    , events_()
{}

void Profiler::shutdown() {
    flush_();
}

void Profiler::set_level(std::string const & level) {
    enabled_ = level == "all";
}


void Profiler::record_event(ProfileEvent && event) {
    if (!event.stop_time.is_set())
        event.stop_time = ProfileTimestamp();
    if (enabled_)
        events_.push_back(std::move(event));
    if (events_.size() >= 100)
        flush_();
}

void Profiler::flush_() {
    if (!events_.empty()) {
        manager_.submit_profile_events(events_);
        events_.clear();
    }
}

} }

