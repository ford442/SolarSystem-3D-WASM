#include "FramePlan.h"
#include "FrameGraph.h"
#include "core/FrameProfile.h"
#include "gfx/rhi/CommandList.h"
#include "gfx/rhi/RhiTypes.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace FramePlan {

namespace {

using FrameGraph::Graph;
using FrameGraph::PassBuilder;
using FrameGraph::ResourceHandle;
using Stage = FrameProfile::Stage;

/**
 * One pass's view and component packed into a word, so an execute lambda captures two
 * words (the sink and this) and std::function keeps it inline: the graph is rebuilt every
 * frame and should not allocate per pass.
 */
std::uint64_t Pack(int view, std::size_t index) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(view)) << 32) | static_cast<std::uint32_t>(index);
}
int ViewOf(std::uint64_t packed) {
    return static_cast<int>(static_cast<std::uint32_t>(packed >> 32));
}
std::uint32_t IndexOf(std::uint64_t packed) {
    return static_cast<std::uint32_t>(packed);
}

class Declarer {
public:
    Declarer(Graph& graph, PassSink& sink, FrameProfile::PassTimings* timings)
        : _graph(graph), _sink(&sink), _timings(timings) {}

    PassBuilder Add(std::string name, const char* kind, Stage stage, FrameGraph::ExecuteFn execute) {
        FrameGraph::PassDesc desc;
        desc.name = std::move(name);
        desc.kind = kind;
        desc.stage = static_cast<int>(stage);
        desc.timerSlot = _timings ? _timings->SlotFor(desc.kind, stage) : -1;
        return _graph.AddPass(std::move(desc), std::move(execute));
    }

    /** A pass drawn once per view: BeginView, then `draw`. */
    template <class Draw>
    PassBuilder AddView(const char* kind, Stage stage, int view, Draw draw) {
        PassSink* sink = _sink;
        const std::uint64_t packed = Pack(view, 0);
        return Add(std::string(kind) + "/" + std::to_string(view), kind, stage,
                   [sink, packed, draw](rhi::CommandList& cmd) {
                       sink->BeginView(cmd, ViewOf(packed));
                       draw(*sink, cmd, ViewOf(packed));
                   });
    }

    PassSink* Sink() const { return _sink; }
    Graph& G() { return _graph; }

private:
    Graph& _graph;
    PassSink* _sink;
    FrameProfile::PassTimings* _timings;
};

} // namespace

rhi::TextureDesc BloomDesc(const rhi::TextureDesc& display) {
    rhi::TextureDesc desc;
    desc.width = display.width / 2;
    desc.height = display.height / 2;
    desc.format = rhi::TextureFormat::Rgba16F;
    desc.displayScale = 0.5f;
    return desc;
}

Resources Build(Graph& graph, const Features& f, PassSink& sink, FrameProfile::PassTimings* timings) {
    graph.Reset();
    Declarer d(graph, sink, timings);
    PassSink* s = &sink;

    Resources r;
    rhi::TextureDesc depthDesc = f.display;
    depthDesc.format = rhi::TextureFormat::Depth24;
    rhi::TextureDesc hdrDesc = f.display;
    hdrDesc.format = rhi::TextureFormat::Rgba16F;
    rhi::TextureDesc particleDesc;
    particleDesc.format = rhi::TextureFormat::Buffer;
    r.backbuffer = graph.Import("Backbuffer", f.display, FrameGraph::ImportFlags{true});
    r.depth = graph.Import("Depth", depthDesc);
    r.shadowMap = graph.Import("ShadowAtlas", f.shadowMap);
    r.hdrColor = graph.Import("HdrColor", hdrDesc);
    r.particles = graph.Import("SolarWindParticles", particleDesc);

    ResourceHandle color = r.backbuffer;
    ResourceHandle depth = r.depth;
    ResourceHandle shadow = r.shadowMap;
    ResourceHandle particles = r.particles;
    // A pass drawing into the scene: onto the colour and depth the passes before it left.
    const auto drawsScene = [&](PassBuilder pass) {
        color = pass.Write(color);
        depth = pass.Write(depth);
    };

    // Advection reads last frame's particles and writes this frame's. It is declared whenever
    // the system exists and survives only if a solar-wind pass below reads the result.
    if (f.solarWindSystem) {
        PassBuilder pass = d.Add("solar-wind-update", "solar-wind-update", Stage::SpaceWeather,
                                 [s](rhi::CommandList& cmd) { s->SolarWindUpdate(cmd); });
        particles = pass.Write(particles);
    }

    // Background, view by view.
    for (int view = 0; view < f.views; ++view) {
        if (f.observe) {
            drawsScene(d.AddView("observe-sky", Stage::Background, view,
                                 [](PassSink& sink, rhi::CommandList& cmd, int v) { sink.ObserveSkyPass(cmd, v); }));
            continue;
        }
        drawsScene(d.AddView("skybox", Stage::Background, view,
                             [](PassSink& sink, rhi::CommandList& cmd, int v) { sink.SkyBoxPass(cmd, v); }));
        if (f.orbitPaths) {
            drawsScene(d.AddView("orbit-paths", Stage::Background, view,
                                 [](PassSink& sink, rhi::CommandList& cmd, int v) { sink.OrbitPaths(cmd, v); }));
        }
        if (f.missionPaths) {
            drawsScene(d.AddView("mission-paths", Stage::Background, view,
                                 [](PassSink& sink, rhi::CommandList& cmd, int v) { sink.MissionPaths(cmd, v); }));
        }
        if (f.xrPointers) {
            drawsScene(d.AddView("xr-pointers", Stage::Background, view,
                                 [](PassSink& sink, rhi::CommandList& cmd, int v) { sink.XrPointers(cmd, v); }));
        }
        if (f.flatCorona) {
            drawsScene(d.AddView("corona", Stage::Background, view,
                                 [](PassSink& sink, rhi::CommandList& cmd, int v) { sink.FlatCorona(cmd, v); }));
        }
    }

    // Scene components: each one's shadow map, then every view's colour pass reading it.
    if (!f.observe && f.views > 0) {
        bool anyCaster = false;
        for (const Component& component : f.components) {
            anyCaster = anyCaster || component.castsShadow;
        }
        if (f.shadowTarget && !f.shadows && anyCaster) {
            PassBuilder pass =
                d.Add("shadow-clear", "shadow", Stage::Shadow, [s](rhi::CommandList& cmd) { s->ClearShadowMap(cmd); });
            shadow = pass.Overwrite(shadow);
        }
        for (std::size_t c = 0; c < f.components.size(); ++c) {
            const Component& component = f.components[c];
            if (f.shadowTarget && f.shadows && component.castsShadow) {
                const std::uint64_t packed = Pack(0, c);
                PassBuilder pass = d.Add("shadow:" + component.name, "shadow", Stage::Shadow,
                                         [s, packed](rhi::CommandList& cmd) { s->ShadowMap(cmd, IndexOf(packed)); });
                shadow = pass.Overwrite(shadow);
            }
            for (int view = 0; view < f.views; ++view) {
                const std::uint64_t packed = Pack(view, c);
                PassBuilder pass = d.Add("scene:" + component.name + "/" + std::to_string(view), "scene", Stage::Scene,
                                         [s, packed](rhi::CommandList& cmd) {
                                             s->BeginView(cmd, ViewOf(packed));
                                             s->SceneComponent(cmd, ViewOf(packed), IndexOf(packed));
                                         });
                pass.Read(shadow);
                drawsScene(pass);
            }
        }
    }

    // Foreground, view by view.
    const rhi::TextureDesc bloomDesc = BloomDesc(f.display);
    for (int view = 0; view < f.views && !f.observe; ++view) {
        if (f.coronaVolume) {
            drawsScene(d.AddView("corona-volume", Stage::Foreground, view,
                                 [](PassSink& sink, rhi::CommandList& cmd, int v) { sink.CoronaVolume(cmd, v); }));
        }
        if (f.asteroids) {
            drawsScene(d.AddView("asteroids", Stage::Foreground, view,
                                 [](PassSink& sink, rhi::CommandList& cmd, int v) { sink.Asteroids(cmd, v); }));
        }
        if (f.solarWind) {
            PassBuilder pass = d.AddView("solar-wind", Stage::Foreground, view,
                                         [](PassSink& sink, rhi::CommandList& cmd, int v) { sink.SolarWind(cmd, v); });
            pass.Read(particles);
            drawsScene(pass);
        }
        if (f.magneticFields) {
            const bool restore = !f.magneticBloom;
            drawsScene(d.AddView(
                "magnetic-fields", Stage::Foreground, view,
                [restore](PassSink& sink, rhi::CommandList& cmd, int v) { sink.MagneticFields(cmd, v, restore); }));
            if (f.magneticBloom) {
                // The bloom chain is a transient: the graph decides which physical chain it is.
                ResourceHandle bloom = graph.Create("BloomChain:fields/" + std::to_string(view), bloomDesc);
                const Graph* g = &graph;
                const std::uint64_t packed = Pack(view, bloom.index);
                PassBuilder capture =
                    d.Add("magnetic-bloom/" + std::to_string(view), "magnetic-bloom", Stage::Foreground,
                          [s, g, packed](rhi::CommandList& cmd) {
                              s->BeginView(cmd, ViewOf(packed));
                              s->MagneticBloom(cmd, ViewOf(packed), g->PhysicalOf({IndexOf(packed), 0}));
                          });
                capture.Read(depth); // the capture starts from the scene's depth
                bloom = capture.Overwrite(bloom);
                PassBuilder composite =
                    d.Add("magnetic-bloom-composite/" + std::to_string(view), "magnetic-bloom", Stage::Foreground,
                          [s, g, packed](rhi::CommandList& cmd) {
                              s->MagneticBloomComposite(cmd, ViewOf(packed), g->PhysicalOf({IndexOf(packed), 0}));
                          });
                composite.Read(bloom);
                color = composite.Write(color);
                if (view == 0) {
                    r.fieldBloom = bloom;
                }
            }
        }
        if (f.auroraBloom) {
            ResourceHandle bloom = graph.Create("BloomChain:aurora/" + std::to_string(view), bloomDesc);
            const Graph* g = &graph;
            const std::uint64_t packed = Pack(view, bloom.index);
            PassBuilder capture = d.Add("aurora-bloom/" + std::to_string(view), "aurora-bloom", Stage::Foreground,
                                        [s, g, packed](rhi::CommandList& cmd) {
                                            s->BeginView(cmd, ViewOf(packed));
                                            s->AuroraBloom(cmd, ViewOf(packed), g->PhysicalOf({IndexOf(packed), 0}));
                                        });
            capture.Read(depth);
            bloom = capture.Overwrite(bloom);
            PassBuilder composite =
                d.Add("aurora-bloom-composite/" + std::to_string(view), "aurora-bloom", Stage::Foreground,
                      [s, g, packed](rhi::CommandList& cmd) {
                          s->AuroraBloomComposite(cmd, ViewOf(packed), g->PhysicalOf({IndexOf(packed), 0}));
                      });
            composite.Read(bloom);
            color = composite.Write(color);
            if (view == 0) {
                r.auroraBloom = bloom;
            }
        }
    }

    // Mono effects: labels at systems not loaded yet, then the star's glow, tone map and flare.
    if (f.proxyLabels) {
        PassBuilder pass = d.Add("proxy-labels", "proxy-labels", Stage::Foreground,
                                 [s](rhi::CommandList& cmd) { s->ProxyLabels(cmd); });
        color = pass.Write(color);
    }
    if (f.starEffects) {
        ResourceHandle hdr = r.hdrColor;
        PassBuilder glow =
            d.Add("star-glow", "star-glow", Stage::Foreground, [s](rhi::CommandList& cmd) { s->StarGlow(cmd); });
        if (f.hdr) {
            hdr = glow.Overwrite(hdr);
            PassBuilder composite = d.Add("hdr-composite", "hdr-composite", Stage::Foreground,
                                          [s](rhi::CommandList& cmd) { s->HdrComposite(cmd); });
            composite.Read(hdr);
            color = composite.Write(color);
        } else {
            color = glow.Write(color);
        }
        PassBuilder flare =
            d.Add("lens-flare", "lens-flare", Stage::Foreground, [s](rhi::CommandList& cmd) { s->LensFlarePass(cmd); });
        color = flare.Write(color);
    }

    // Screen-space overlays.
    if (f.observeHud) {
        color =
            d.Add("observe-hud", "hud", Stage::Hud, [s](rhi::CommandList& cmd) { s->ObserveHud(cmd); }).Write(color);
    }
    if (f.distances) {
        color = d.Add("distances", "hud", Stage::Hud, [s](rhi::CommandList& cmd) { s->Distances(cmd); }).Write(color);
    }
    if (f.hints) {
        color = d.Add("hints", "hud", Stage::Hud, [s](rhi::CommandList& cmd) { s->Hints(cmd); }).Write(color);
    }
    if (f.screenFade) {
        color = d.Add("screen-fade", "screen-fade", Stage::Hud, [s](rhi::CommandList& cmd) {
                     s->ScreenFade(cmd);
                 }).Write(color);
    }
    if (f.uploads) {
        d.Add("uploads", "uploads", Stage::Uploads, [s](rhi::CommandList& cmd) { s->Uploads(cmd); }).SideEffect();
    }
    if (f.loadingProgress) {
        // Also reports streaming progress to the page, drawn or not.
        PassBuilder pass =
            d.Add("loading-progress", "hud", Stage::Hud, [s](rhi::CommandList& cmd) { s->LoadingProgress(cmd); });
        pass.SideEffect();
        color = pass.Write(color);
    }
    return r;
}

} // namespace FramePlan
