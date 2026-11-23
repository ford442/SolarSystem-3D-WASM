#ifndef SOLARSYSTEM_SPACEWEATHERCONTROLLER_H
#define SOLARSYSTEM_SPACEWEATHERCONTROLLER_H
#include "FrameContext.h"
#include "IAppModule.h"
#include "SimState.h"
#include "astro/SpaceWeather.h"
#include "core/FrameProfile.h"
#include <string>

/**
 * Space weather, Application's side (docs/ARCHITECTURE.md §13): the model in astro/SpaceWeather
 * evaluated at this frame's date and mapped into the scene (SpaceWeatherFrame, which Contribute
 * hands the renderer), plus the settings-panel API. Nothing here touches GL.
 */
class SpaceWeatherController final : public IAppModule {
public:
    explicit SpaceWeatherController(const ModuleContext& app);

    const char* Name() const override { return "space-weather"; }
    FrameProfile::Stage Stage() const override { return FrameProfile::Stage::SpaceWeather; }
    /** The CME list and the star's cycle, once (and again after a star-system swap). */
    void Init();
    /** The solar-wind particles (the settings panel's "Solar wind"); auroras draw either way. */
    void SetSolarWindEnabled(bool enabled);
    bool SolarWindEnabled() const { return _wind; }
    /** < 0: Kp from the wind; 0–9 holds every oval at that Kp. */
    void SetKpOverride(double kp);
    double KpOverride() const { return _model.KpOverride(); }
    /** An Earth-directed CME leaving the Sun now; turns the wind on so it shows. */
    void LaunchCmeNow();
    /** Play catalog event `index`: the date to its launch, the wind on, the orrery slowed. */
    bool PlayEvent(int index, SimState& sim);
    /** Set the date to an ISO-8601 UTC instant with the wind on. */
    bool JumpToDate(const std::string& isoUtc);
    /** SpaceWeather::State::ToJson for this frame, with the renderer's settings. */
    std::string ToJson(const SimState& sim) const;

    /** The model at this frame's date, mapped into the scene. */
    void Update(SimState& sim) override;
    /** SceneSnapshot::spaceWeather: this frame's SpaceWeatherFrame, or null with no star. */
    void Contribute(SceneSnapshot& scene) override;

private:
    const ModuleContext& _app;
    SpaceWeather::State _model;
    SpaceWeatherFrame _frame;
    bool _active = false; // _frame holds this frame (there is a star)
    bool _wind = false;
    bool _loaded = false;
    double _clock = 0.0; // visual seconds the particles have run
};

#endif // SOLARSYSTEM_SPACEWEATHERCONTROLLER_H
