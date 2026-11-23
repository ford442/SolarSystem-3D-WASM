#include "SkyEventCache.h"
#include "Solar_System/OrbitLayout.h"
#include "Solar_System/StarSystemCatalog.h"
#include "astro/SkyEvents.h"

namespace {
// How long to wait before retrying a search that found nothing, in simulated days. Only
// reachable if the search window is ever shortened; the default window always hits.
constexpr double kEmptyRetryDays = 30.0;
} // namespace

const SkyEvents::Conjunction& SkyEventCache::NextConjunction() {
    if (!StarSystemCatalog::IsSol()) {
        static const SkyEvents::Conjunction kNone{};
        return kNone;
    }
    const double jd = OrbitLayout::GetJulianDate();
    const bool passed = _conjunction.valid && jd > _conjunction.julianDate;
    const bool rewound = jd < _conjunctionJd - 0.5;
    const bool retryEmpty = !_conjunction.valid && jd > _conjunctionJd + kEmptyRetryDays;
    if (!_conjunctionCached || passed || rewound || retryEmpty) {
        _conjunction = SkyEvents::NextInnerPlanetConjunction(jd);
        _conjunctionJd = jd;
        _conjunctionCached = true;
    }
    return _conjunction;
}

const SkyEvents::SkyEvent& SkyEventCache::NextEvent() {
    if (!StarSystemCatalog::IsSol()) {
        static const SkyEvents::SkyEvent kNone{};
        return kNone;
    }
    const double jd = OrbitLayout::GetJulianDate();
    const bool passed = _event.valid && jd > _event.julianDate;
    const bool rewound = jd < _eventJd - 0.5;
    const bool retryEmpty = !_event.valid && jd > _eventJd + kEmptyRetryDays;
    if (!_eventCached || passed || rewound || retryEmpty) {
        _event = SkyEvents::NextEvent(jd);
        _eventJd = jd;
        _eventCached = true;
    }
    return _event;
}
