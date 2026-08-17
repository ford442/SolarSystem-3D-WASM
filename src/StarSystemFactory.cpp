#include "Application.h"
#include "QualitySettings.h"
#include <chrono>
#include <random>

using namespace std;

void Application::InitStarSystem() {
    _sphereModel = std::make_unique<MeshHolder>("resource/models/sphere.obj");

    _starGlowShader = make_unique<Shader>("resource/shaders/starGlow.vs", "resource/shaders/starGlow.fs");
    StarInfo sunInfo(*_sphereModel, *_mainStarShader, *_starGlowShader, TextureImage2D("resource/textures_low/Star_Spectrum_Low.dds"),
                     _starTemperatureInKelvin, 696342.0, glm::vec3(0.99607843, 0.890196078, 0.725490196), L"Sun", L"Солнце");
    _sun = make_shared<Sun>(sunInfo);
    _sun->SetMagneticField(MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Sun));

#ifdef __EMSCRIPTEN__
    LoadPlanetSystemManifests();
#else
    // Desktop: load all immediately (no memory constraint)
    InitMercury(*_sphereModel);
    InitVenus(*_sphereModel);
    InitEarthSystem(*_sphereModel);
    InitMarsSystem(*_sphereModel);
    InitJupiterSystem(*_sphereModel);
    InitSaturnSystem(*_sphereModel);
    InitUranusSystem(*_sphereModel);
    InitNeptuneSystem(*_sphereModel);
    InitPlutoSystem(*_sphereModel);
#endif
}

void Application::InitMercury(const MeshHolder& sphereModel) {
    PlanetInfo mercuryInfo(sphereModel, 0.38, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath(TexturePaths::Mercury::Diffuse.low, TexturePaths::Mercury::Diffuse.high)),
            }, TextureImage2D(GetTexturePath(TexturePaths::Mercury::Normal.low, TexturePaths::Mercury::Normal.high)), L"Mercury", L"Меркурий", TextureImage2D(GetTexturePath(TexturePaths::Mercury::Specular.low, TexturePaths::Mercury::Specular.high)));
    shared_ptr<Planet> mercury = make_shared<Mercury>(mercuryInfo, _sun);
    mercury->SetMagneticField(MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Mercury));

    const glm::mat4 lightProjection = glm::ortho(-mercury->GetRadius() * 3.0f, mercury->GetRadius() * 3.0f, -mercury->GetRadius() * 3.0f, mercury->GetRadius() * 3.0f, _camera.GetNear(), _camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), mercury->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableSceneComponent mercurySystemComponent;
    mercurySystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    mercurySystemComponent.planet = move(mercury);
    _renderableSceneComponents.push_back(move(mercurySystemComponent));
}

void Application::InitVenus(const MeshHolder& sphereModel) {
    PlanetInfo venusInfo(sphereModel, 0.95, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath(TexturePaths::Venus::Diffuse.low, TexturePaths::Venus::Diffuse.high)),
            }, TextureImage2D(GetTexturePath(TexturePaths::Venus::Normal.low, TexturePaths::Venus::Normal.high)), L"Venus", L"Венера");
    shared_ptr<Planet> venus = make_shared<Venus>(venusInfo, _sun);
    venus->SetMagneticField(MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Venus));

    AtmosphereInfo venusAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 1.1, glm::vec3(203/255.f, 158/255.f, 69/255.), venus->GetRadius() - 0.00007, 1.995);
    unique_ptr<Atmosphere> venusAtmosphere = make_unique<Atmosphere>(venusAtmosphereInfo, venus);

    const glm::mat4 lightProjection = glm::ortho(-venus->GetRadius() * 3.0f, venus->GetRadius() * 3.0f, -venus->GetRadius() * 3.0f, venus->GetRadius() * 3.0f, _camera.GetNear(), _camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), venus->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderableVenusAtmosphere;
    renderableVenusAtmosphere.atmosphere = move(venusAtmosphere);
    renderableVenusAtmosphere.hScaleFactor = 6.0;
    renderableVenusAtmosphere.parentEarthSizeCoefficient = venus->GetEarthSizeCoefficient();

    RenderableSceneComponent venusSystemComponent;
    venusSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    venusSystemComponent.planet = move(venus);
    venusSystemComponent.atmospheres.push_back(move(renderableVenusAtmosphere));
    _renderableSceneComponents.push_back(move(venusSystemComponent));
}

void Application::InitEarthSystem(const MeshHolder& sphereModel) {
    // Load textures with low-res fallback for Web
    PlanetInfo earthInfo(sphereModel, 1.0, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath(TexturePaths::Earth::Diffuse.low, TexturePaths::Earth::Diffuse.high)),
                TextureImage2D("resource/textures_low/Earth_Clouds_Diffuse_Low.dds"),
                TextureImage2D("resource/textures_low/Earth_Night_Diffuse_Low.dds"),
            }, TextureImage2D(GetTexturePath(TexturePaths::Earth::Normal.low, TexturePaths::Earth::Normal.high)), L"Earth", L"Земля", TextureImage2D(GetTexturePath(TexturePaths::Earth::Specular.low, TexturePaths::Earth::Specular.high)));
    shared_ptr<Planet> earth = make_shared<Earth>(earthInfo, _sun);
    earth->SetMagneticField(MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Earth));

    SatelliteInfo moonInfo(sphereModel, 0.2724, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Moon::Diffuse.low, TexturePaths::Moon::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Moon::Normal.low, TexturePaths::Moon::Normal.high)),
                           L"Moon", L"Луна");
    shared_ptr<Satellite> moon = make_shared<Moon>(moonInfo, earth);

    AtmosphereInfo earthAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 1.1, glm::vec3(0.3, 0.7, 1.0), earth->GetRadius() - 0.00007, 2.1);
    unique_ptr<Atmosphere> earthAtmosphere = make_unique<Atmosphere>(earthAtmosphereInfo, earth);

    CloudsInfo earthCloudsInfo(sphereModel, *_mainCloudsShader, 1.0055, TextureImage2D("resource/textures_low/Earth_Clouds_Diffuse_Low.dds"),
                               TextureImage2D("resource/textures_low/Earth_Clouds_Normal_Low.dds"));
    unique_ptr<Clouds> earthClouds = make_unique<EarthClouds>(earthCloudsInfo, earth);

    const glm::mat4 lightProjection = glm::ortho(-earth->GetRadius() * 3.0f, earth->GetRadius() * 3.0f, -earth->GetRadius() * 3.0f, earth->GetRadius() * 3.0f, _camera.GetNear(), _camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), earth->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderableEarthAtmosphere;
    renderableEarthAtmosphere.atmosphere = move(earthAtmosphere);
    renderableEarthAtmosphere.hScaleFactor = 6.0;
    renderableEarthAtmosphere.parentEarthSizeCoefficient = earth->GetEarthSizeCoefficient();

    RenderableSceneComponent earthSystemComponent;
    earthSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    earthSystemComponent.planet = move(earth);
    earthSystemComponent.satellites = vector<shared_ptr<Satellite>>{move(moon)};
    earthSystemComponent.atmospheres.push_back(move(renderableEarthAtmosphere));
    earthSystemComponent.clouds = move(earthClouds);
    _renderableSceneComponents.push_back(move(earthSystemComponent));
}

void Application::InitMarsSystem(const MeshHolder& sphereModel) {
    MeshHolder phobosModel("resource/models/phobos.obj"), deimosModel("resource/models/deimos.obj");

    PlanetInfo marsInfo(sphereModel, 0.53, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath(TexturePaths::Mars::Diffuse.low, TexturePaths::Mars::Diffuse.high)),
            }, TextureImage2D(GetTexturePath(TexturePaths::Mars::Normal.low, TexturePaths::Mars::Normal.high)), L"Mars", L"Марс");
    shared_ptr<Planet> mars = make_shared<Mars>(marsInfo, _sun);
    mars->SetMagneticField(MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Mars));

    SatelliteInfo phobosInfo(phobosModel, 0.001768, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Phobos::Diffuse.low, TexturePaths::Phobos::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Phobos::Normal.low, TexturePaths::Phobos::Normal.high)),
                             L"Phobos", L"Фобос");
    SatelliteInfo deimosInfo(deimosModel, 0.00097316, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Deimos::Diffuse.low, TexturePaths::Deimos::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Deimos::Normal.low, TexturePaths::Deimos::Normal.high)),
                             L"Deimos", L"Деймос");
    shared_ptr<Satellite> phobos = make_shared<Phobos>(phobosInfo, mars);
    shared_ptr<Satellite> deimos = make_shared<Deimos>(deimosInfo, mars);

    AtmosphereInfo marsAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 0.583, glm::vec3(0.976, 0.302, 0.208), mars->GetRadius() - 0.00007, 1.113);
    unique_ptr<Atmosphere> marsAtmosphere = make_unique<Atmosphere>(marsAtmosphereInfo, mars);

    const glm::mat4 lightProjection = glm::ortho(-mars->GetRadius() * 3.0f, mars->GetRadius() * 3.0f, -mars->GetRadius() * 3.0f, mars->GetRadius() * 3.0f, _camera.GetNear(),
                                                 glm::length(_sun->GetPosition() - mars->GetPosition()) + 50.f);
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), mars->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderableMarsAtmosphere;
    renderableMarsAtmosphere.atmosphere = move(marsAtmosphere);
    renderableMarsAtmosphere.hScaleFactor = 6.0;
    renderableMarsAtmosphere.parentEarthSizeCoefficient = mars->GetEarthSizeCoefficient();

    RenderableSceneComponent marsSystemComponent;
    marsSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    marsSystemComponent.planet = move(mars);
    marsSystemComponent.satellites = vector<shared_ptr<Satellite>>{move(phobos), move(deimos)};
    marsSystemComponent.atmospheres.push_back(move(renderableMarsAtmosphere));
    _renderableSceneComponents.push_back(move(marsSystemComponent));
}

void Application::InitJupiterSystem(const MeshHolder& sphereModel) {
    PlanetInfo jupiterInfo(sphereModel, 11.2, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath(TexturePaths::Jupiter::Diffuse.low, TexturePaths::Jupiter::Diffuse.high)),
            }, TextureImage2D(GetTexturePath(TexturePaths::Jupiter::Normal.low, TexturePaths::Jupiter::Normal.high)), L"Jupiter", L"Юпитер");
    shared_ptr<Planet> jupiter = make_shared<Jupiter>(jupiterInfo, _sun);
    jupiter->SetMagneticField(MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Jupiter));

    SatelliteInfo ioInfo(sphereModel, 0.28592, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Io::Diffuse.low, TexturePaths::Io::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Io::Normal.low, TexturePaths::Io::Normal.high)),
                         L"Io", L"Ио");
    SatelliteInfo europaInfo(sphereModel, 0.244985, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Europa::Diffuse.low, TexturePaths::Europa::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Europa::Normal.low, TexturePaths::Europa::Normal.high)),
                             L"Europa", L"Европа");
    SatelliteInfo ganymedeInfo(sphereModel, 0.41345, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Ganymede::Diffuse.low, TexturePaths::Ganymede::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Ganymede::Normal.low, TexturePaths::Ganymede::Normal.high)),
                               L"Ganymede", L"Ганимед");
    SatelliteInfo callistoInfo(sphereModel, 0.3783236, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Callisto::Diffuse.low, TexturePaths::Callisto::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Callisto::Normal.low, TexturePaths::Callisto::Normal.high)),
                               L"Callisto", L"Каллисто");
    shared_ptr<Satellite> io = make_shared<Io>(ioInfo, jupiter);
    shared_ptr<Satellite> europa = make_shared<Europa>(europaInfo, jupiter);
    shared_ptr<Satellite> ganymede = make_shared<Ganymede>(ganymedeInfo, jupiter);
    shared_ptr<Satellite> callisto = make_shared<Callisto>(callistoInfo, jupiter);

    AtmosphereInfo jupiterAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 11.4, glm::vec3(153.f/255, 139.f/255, 120.f/255), jupiter->GetRadius() - 0.00007, 23.35);
    unique_ptr<Atmosphere> jupiterAtmosphere = make_unique<Atmosphere>(jupiterAtmosphereInfo, jupiter);

    const glm::mat4 lightProjection = glm::ortho(-jupiter->GetRadius() * 3.0f, jupiter->GetRadius() * 3.0f, -jupiter->GetRadius() * 3.0f, jupiter->GetRadius() * 3.0f, _camera.GetNear(), _camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), jupiter->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderableJupiterAtmosphere;
    renderableJupiterAtmosphere.atmosphere = move(jupiterAtmosphere);
    renderableJupiterAtmosphere.hScaleFactor = 26.0;
    renderableJupiterAtmosphere.parentEarthSizeCoefficient = jupiter->GetEarthSizeCoefficient();
    renderableJupiterAtmosphere.isUseToneMapping = true;

    RenderableSceneComponent jupiterSystemComponent;
    jupiterSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    jupiterSystemComponent.planet = move(jupiter);
    jupiterSystemComponent.satellites = vector<shared_ptr<Satellite>>{move(io), move(europa), move(ganymede), move(callisto)};
    jupiterSystemComponent.atmospheres.push_back(move(renderableJupiterAtmosphere));
    _renderableSceneComponents.push_back(move(jupiterSystemComponent));
}

void Application::InitSaturnSystem(const MeshHolder& sphereModel) {
    MeshHolder saturnRingModel("resource/models/saturn_ring.obj");

    PlanetInfo saturnInfo(sphereModel, 9.14, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath(TexturePaths::Saturn::Diffuse.low, TexturePaths::Saturn::Diffuse.high)),
            }, TextureImage2D(GetTexturePath(TexturePaths::Saturn::Normal.low, TexturePaths::Saturn::Normal.high)), L"Saturn", L"Сатурн");
    shared_ptr<Planet> saturn = make_shared<Saturn>(saturnInfo, _sun);
    saturn->SetMagneticField(MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Saturn));

    PlanetaryRingInfo saturnRingInfo(saturnRingModel, 22.0, 43.7, *_mainPlanetShader, TextureImage2D("resource/textures_low/Saturn_Rings_Low.dds")); 
    unique_ptr<PlanetaryRing> saturnRing = make_unique<SaturnRing>(saturnRingInfo, saturn);

    SatelliteInfo mimasInfo(sphereModel, 0.03111, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Mimas::Diffuse.low, TexturePaths::Mimas::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Mimas::Normal.low, TexturePaths::Mimas::Normal.high)),
                            L"Mimas", L"Мимас");
    SatelliteInfo enceladusInfo(sphereModel, 0.03957, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Enceladus::Diffuse.low, TexturePaths::Enceladus::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Enceladus::Normal.low, TexturePaths::Enceladus::Normal.high)),
                            L"Enceladus", L"Энцелад");
    SatelliteInfo tethysInfo(sphereModel, 0.083346, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Tethys::Diffuse.low, TexturePaths::Tethys::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Tethys::Normal.low, TexturePaths::Tethys::Normal.high)),
                            L"Tethys", L"Тефия");
    SatelliteInfo dioneInfo(sphereModel, 0.08812, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Dione::Diffuse.low, TexturePaths::Dione::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Dione::Normal.low, TexturePaths::Dione::Normal.high)),
                            L"Dione", L"Диона");
    SatelliteInfo rheaInfo(sphereModel, 0.119886, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Rhea::Diffuse.low, TexturePaths::Rhea::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Rhea::Normal.low, TexturePaths::Rhea::Normal.high)),
                            L"Rhea", L"Рея");
    SatelliteInfo titanInfo(sphereModel, 0.404136, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Titan::Diffuse.low, TexturePaths::Titan::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Titan::Normal.low, TexturePaths::Titan::Normal.high)),
                            L"Titan", L"Титан");
    SatelliteInfo iapetusInfo(sphereModel, 0.115288, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Iapetus::Diffuse.low, TexturePaths::Iapetus::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Iapetus::Normal.low, TexturePaths::Iapetus::Normal.high)),
                            L"Iapetus", L"Япет");
    shared_ptr<Satellite> mimas = make_shared<Mimas>(mimasInfo, saturn);
    shared_ptr<Satellite> enceladus = make_shared<Enceladus>(enceladusInfo, saturn);
    shared_ptr<Satellite> tethys = make_shared<Tethys>(tethysInfo, saturn);
    shared_ptr<Satellite> dione = make_shared<Dione>(dioneInfo, saturn);
    shared_ptr<Satellite> rhea = make_shared<Rhea>(rheaInfo, saturn);
    shared_ptr<Satellite> titan = make_shared<Titan>(titanInfo, saturn);
    shared_ptr<Satellite> iapetus = make_shared<Iapetus>(iapetusInfo, saturn);

    AtmosphereInfo saturnAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 9.34, glm::vec3(84.f/255, 132.f/255, 176.f/255), saturn->GetRadius() - 0.00007, 18.6);
    unique_ptr<Atmosphere> saturnAtmosphere = make_unique<Atmosphere>(saturnAtmosphereInfo, saturn);

    AtmosphereInfo titanAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 0.504136, glm::vec3(40.f/255, 33.f/255, 72.f/255), titan->GetRadius() - 0.00007, 0.8429210,
                                       glm::vec3(0.36862745, 0.0666667, 0.0196078)); 
    unique_ptr<Atmosphere> titanAtmosphere = make_unique<Atmosphere>(titanAtmosphereInfo, titan);

    const glm::mat4 lightProjection = glm::ortho(-saturn->GetRadius() * 3.0f, saturn->GetRadius() * 3.0f, -saturn->GetRadius() * 3.0f, saturn->GetRadius() * 3.0f, _camera.GetNear(), _camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), saturn->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderableSaturnAtmosphere;
    renderableSaturnAtmosphere.atmosphere = move(saturnAtmosphere);
    renderableSaturnAtmosphere.hScaleFactor = 27.0;
    renderableSaturnAtmosphere.parentEarthSizeCoefficient = saturn->GetEarthSizeCoefficient();
    renderableSaturnAtmosphere.isUseToneMapping = true;

    RenderableAtmosphere renderableTitanAtmosphere;
    renderableTitanAtmosphere.atmosphere = move(titanAtmosphere);
    renderableTitanAtmosphere.hScaleFactor = 4.8;
    renderableTitanAtmosphere.parentEarthSizeCoefficient = titan->GetEarthSizeCoefficient();

    RenderableSceneComponent saturnSystemComponent;
    saturnSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    saturnSystemComponent.planet = move(saturn);
    saturnSystemComponent.satellites = vector<shared_ptr<Satellite>>{move(mimas), move(enceladus), move(tethys), move(dione), move(rhea), move(titan), move(iapetus)};
    saturnSystemComponent.atmospheres.push_back(move(renderableSaturnAtmosphere));
    saturnSystemComponent.atmospheres.push_back(move(renderableTitanAtmosphere));
    saturnSystemComponent.planetaryRing = move(saturnRing);
    _renderableSceneComponents.push_back(move(saturnSystemComponent));
}

void Application::InitUranusSystem(const MeshHolder& sphereModel) {
    MeshHolder uranusRingModel("resource/models/uranus_ring.obj");

    PlanetInfo uranusInfo(sphereModel, 3.98085, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath(TexturePaths::Uranus::Diffuse.low, TexturePaths::Uranus::Diffuse.high)),
                TextureImage2D("resource/textures_low/Uranus_Clouds_Diffuse_Low.dds")
            }, TextureImage2D(GetTexturePath(TexturePaths::Uranus::Normal.low, TexturePaths::Uranus::Normal.high)), L"Uranus", L"Уран");
    shared_ptr<Planet> uranus = make_shared<Uranus>(uranusInfo, _sun);
    uranus->SetMagneticField(MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Uranus));
    PlanetaryRingInfo uranusRingInfo(uranusRingModel, 12.6, 16.0, *_mainPlanetShader, TextureImage2D("resource/textures_low/Uranus_Rings_Low.dds")); // Radiuses from 3D model
    unique_ptr<PlanetaryRing> uranusRing = make_unique<UranusRing>(uranusRingInfo, uranus);

    SatelliteInfo mirandaInfo(sphereModel, 0.0368858, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Miranda::Diffuse.low, TexturePaths::Miranda::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Miranda::Normal.low, TexturePaths::Miranda::Normal.high)),
                            L"Miranda", L"Миранда");
    SatelliteInfo arielInfo(sphereModel, 0.090865, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Ariel::Diffuse.low, TexturePaths::Ariel::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Ariel::Normal.low, TexturePaths::Ariel::Normal.high)),
                            L"Ariel", L"Ариэль");
    SatelliteInfo umbrielInfo(sphereModel, 0.091775, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Umbriel::Diffuse.low, TexturePaths::Umbriel::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Umbriel::Normal.low, TexturePaths::Umbriel::Normal.high)),
                            L"Umbriel", L"Умбриэль");
    SatelliteInfo titaniaInfo(sphereModel, 0.123748, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Titania::Diffuse.low, TexturePaths::Titania::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Titania::Normal.low, TexturePaths::Titania::Normal.high)),
                            L"Titania", L"Титания");
    SatelliteInfo oberonInfo(sphereModel, 0.11951, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Oberon::Diffuse.low, TexturePaths::Oberon::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Oberon::Normal.low, TexturePaths::Oberon::Normal.high)),
                            L"Oberon", L"Оберон");
    shared_ptr<Satellite> miranda = make_shared<Miranda>(mirandaInfo, uranus);
    shared_ptr<Satellite> ariel = make_shared<Ariel>(arielInfo, uranus);
    shared_ptr<Satellite> umbriel = make_shared<Umbriel>(umbrielInfo, uranus);
    shared_ptr<Satellite> titania = make_shared<Titania>(titaniaInfo, uranus);
    shared_ptr<Satellite> oberon = make_shared<Oberon>(oberonInfo, uranus);

    CloudsInfo uranusCloudsInfo(sphereModel, *_mainCloudsShader, 3.98635, TextureImage2D("resource/textures_low/Uranus_Clouds_Diffuse_Low.dds"),
                            TextureImage2D("resource/textures_low/Uranus_Clouds_Normal_Low.dds"));
    unique_ptr<Clouds> uranusClouds = make_unique<UranusClouds>(uranusCloudsInfo, uranus);

    AtmosphereInfo uranusAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 4.0, glm::vec3(45.f/255, 101.f/255, 114.f/255), uranus->GetRadius() - 0.00007, 8.1);
    unique_ptr<Atmosphere> uranusAtmosphere = make_unique<Atmosphere>(uranusAtmosphereInfo, uranus);

    const glm::mat4 lightProjection = glm::ortho(-uranus->GetRadius() * 3.0f, uranus->GetRadius() * 3.0f, -uranus->GetRadius() * 3.0f, uranus->GetRadius() * 3.0f, _camera.GetNear(), _camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), uranus->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderableUranusAtmosphere;
    renderableUranusAtmosphere.atmosphere = move(uranusAtmosphere);
    renderableUranusAtmosphere.hScaleFactor = 24.0;
    renderableUranusAtmosphere.parentEarthSizeCoefficient = uranus->GetEarthSizeCoefficient();
    renderableUranusAtmosphere.isUseToneMapping = true;

    RenderableSceneComponent uranusSystemComponent;
    uranusSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    uranusSystemComponent.planet = move(uranus);
    uranusSystemComponent.satellites = vector<shared_ptr<Satellite>>{move(miranda), move(ariel), move(umbriel), move(titania), move(oberon)};
    uranusSystemComponent.atmospheres.push_back(move(renderableUranusAtmosphere));
    uranusSystemComponent.clouds = move(uranusClouds);
    uranusSystemComponent.planetaryRing = move(uranusRing);
    _renderableSceneComponents.push_back(move(uranusSystemComponent));
}

void Application::InitNeptuneSystem(const MeshHolder& sphereModel) {
    PlanetInfo neptuneInfo(sphereModel, 3.8647, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath(TexturePaths::Neptune::Diffuse.low, TexturePaths::Neptune::Diffuse.high)),
                TextureImage2D("resource/textures_low/Neptune_Clouds_Diffuse_Low.dds")
            }, TextureImage2D(GetTexturePath(TexturePaths::Neptune::Normal.low, TexturePaths::Neptune::Normal.high)), L"Neptune", L"Нептун");
    shared_ptr<Planet> neptune = make_shared<Neptune>(neptuneInfo, _sun);
    neptune->SetMagneticField(MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Neptune));

    SatelliteInfo tritonInfo(sphereModel, 0.2724, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Triton::Diffuse.low, TexturePaths::Triton::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Triton::Normal.low, TexturePaths::Triton::Normal.high)),
                             L"Triton", L"Тритон");
    shared_ptr<Satellite> triton = make_shared<Triton>(tritonInfo, neptune);

    CloudsInfo neptuneCloudsInfo(sphereModel, *_mainCloudsShader, 3.87, TextureImage2D("resource/textures_low/Neptune_Clouds_Diffuse_Low.dds"),
                                TextureImage2D("resource/textures_low/Neptune_Clouds_Normal_Low.dds"));
    unique_ptr<Clouds> neptuneClouds = make_unique<NeptuneClouds>(neptuneCloudsInfo, neptune);

    AtmosphereInfo neptuneAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 3.9, glm::vec3(62.f/255, 92.f/255, 169.f/255), neptune->GetRadius() - 0.00007, 7.9);
    unique_ptr<Atmosphere> neptuneAtmosphere = make_unique<Atmosphere>(neptuneAtmosphereInfo, neptune);

    const glm::mat4 lightProjection = glm::ortho(-neptune->GetRadius() * 3.0f, neptune->GetRadius() * 3.0f, -neptune->GetRadius() * 3.0f, neptune->GetRadius() * 3.0f, _camera.GetNear(), _camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), neptune->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderableNeptuneAtmosphere;
    renderableNeptuneAtmosphere.atmosphere = move(neptuneAtmosphere);
    renderableNeptuneAtmosphere.hScaleFactor = 23.0;
    renderableNeptuneAtmosphere.parentEarthSizeCoefficient = neptune->GetEarthSizeCoefficient();
    renderableNeptuneAtmosphere.isUseToneMapping = true;

    RenderableSceneComponent neptuneSystemComponent;
    neptuneSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    neptuneSystemComponent.planet = move(neptune);
    neptuneSystemComponent.satellites = vector<shared_ptr<Satellite>>{move(triton)};
    neptuneSystemComponent.atmospheres.push_back(move(renderableNeptuneAtmosphere));
    neptuneSystemComponent.clouds = move(neptuneClouds);
    _renderableSceneComponents.push_back(move(neptuneSystemComponent));
}

void Application::InitPlutoSystem(const MeshHolder& sphereModel) {
    PlanetInfo plutoInfo(sphereModel, 0.18651, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath(TexturePaths::Pluto::Diffuse.low, TexturePaths::Pluto::Diffuse.high)),
            }, TextureImage2D(GetTexturePath(TexturePaths::Pluto::Normal.low, TexturePaths::Pluto::Normal.high)), L"Pluto", L"Плутон", TextureImage2D(GetTexturePath(TexturePaths::Pluto::Specular.low, TexturePaths::Pluto::Specular.high)));
    shared_ptr<Planet> pluto = make_shared<Pluto>(plutoInfo, _sun);
    pluto->SetMagneticField(MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Pluto));

    SatelliteInfo charonInfo(sphereModel, 0.09512, *_mainPlanetShader, {TextureImage2D(GetTexturePath(TexturePaths::Charon::Diffuse.low, TexturePaths::Charon::Diffuse.high))}, TextureImage2D(GetTexturePath(TexturePaths::Charon::Normal.low, TexturePaths::Charon::Normal.high)),
                             L"Charon", L"Харон", TextureImage2D(GetTexturePath(TexturePaths::Charon::Specular.low, TexturePaths::Charon::Specular.high)));
    shared_ptr<Satellite> charon  = make_shared<Charon>(charonInfo, pluto);

    AtmosphereInfo plutoAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 0.45, glm::vec3(92.f/255, 120.f/255, 141.f/255), pluto->GetRadius(), 1.0,
                                       glm::vec3(35.f/255, 52.f/255, 220.f/255));
    unique_ptr<Atmosphere> plutoAtmosphere = make_unique<Atmosphere>(plutoAtmosphereInfo, pluto);

    const glm::mat4 lightProjection = glm::ortho(-pluto->GetRadius() * 3.0f, pluto->GetRadius() * 3.0f, -pluto->GetRadius() * 3.0f, pluto->GetRadius() * 3.0f, _camera.GetNear(), _camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), pluto->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderablePlutoAtmosphere;
    renderablePlutoAtmosphere.atmosphere = move(plutoAtmosphere);
    renderablePlutoAtmosphere.hScaleFactor = 16.0;
    renderablePlutoAtmosphere.parentEarthSizeCoefficient = pluto->GetEarthSizeCoefficient();
    renderablePlutoAtmosphere.isUseToneMapping = true;

    RenderableSceneComponent plutoSystemComponent;
    plutoSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    plutoSystemComponent.planet = move(pluto);
    plutoSystemComponent.satellites = vector<shared_ptr<Satellite>>{move(charon)};
    plutoSystemComponent.atmospheres.push_back(move(renderablePlutoAtmosphere));
    _renderableSceneComponents.push_back(move(plutoSystemComponent));
}
