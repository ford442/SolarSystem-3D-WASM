#ifndef SOLARSYSTEM_SKYEVENTCACHE_H
#define SOLARSYSTEM_SKYEVENTCACHE_H
#include "astro/SkyEvents.h"

/**
 * The next sky events at the current simulation epoch, for the hint line and the page. The
 * searches cost a couple of milliseconds, so each result is kept until its event passes, time
 * is scrubbed backwards, or (after a search that found nothing) 30 quiet days go by. Outside
 * Sol there are none: the searches are Sol's ephemeris.
 */
class SkyEventCache {
public:
    /** Next inner-planet conjunction. */
    const SkyEvents::Conjunction& NextConjunction();
    /** Next sky event of any kind (conjunction, eclipse, transit, shadow transit). */
    const SkyEvents::SkyEvent& NextEvent();
    /** Search afresh next time (another star system was loaded). */
    void Reset() {
        _conjunctionCached = false;
        _eventCached = false;
    }

private:
    SkyEvents::Conjunction _conjunction;
    double _conjunctionJd = 0.0;
    bool _conjunctionCached = false;
    SkyEvents::SkyEvent _event;
    double _eventJd = 0.0;
    bool _eventCached = false;
};

#endif // SOLARSYSTEM_SKYEVENTCACHE_H
