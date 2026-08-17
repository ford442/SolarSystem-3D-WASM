#pragma once

namespace TexturePaths {
struct Pair {
    const char* low;
    const char* mid;
    const char* high;
};

#define SOLAR_TEXTURE_PAIR(Name, Base) \
    inline constexpr Pair Name{ \
        "resource/textures_low/" Base "_Low.dds", \
        "resource/textures_mid/" Base "_Mid.dds", \
        "resource/textures/" Base ".dds"}

namespace Mercury { SOLAR_TEXTURE_PAIR(Diffuse, "Mercury_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Mercury_Normal"); SOLAR_TEXTURE_PAIR(Specular, "Mercury_Specular"); }
namespace Venus { SOLAR_TEXTURE_PAIR(Diffuse, "Venus_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Venus_Normal"); }
namespace Earth { SOLAR_TEXTURE_PAIR(Diffuse, "Earth_Day_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Earth_Normal"); SOLAR_TEXTURE_PAIR(Specular, "Earth_Specular"); }
namespace Mars { SOLAR_TEXTURE_PAIR(Diffuse, "Mars_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Mars_Normal"); }
namespace Jupiter { SOLAR_TEXTURE_PAIR(Diffuse, "Jupiter_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Jupiter_Normal"); }
namespace Saturn { SOLAR_TEXTURE_PAIR(Diffuse, "Saturn_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Saturn_Normal"); }
namespace Uranus { SOLAR_TEXTURE_PAIR(Diffuse, "Uranus_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Uranus_Normal"); }
namespace Neptune { SOLAR_TEXTURE_PAIR(Diffuse, "Neptune_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Neptune_Normal"); }
namespace Pluto { SOLAR_TEXTURE_PAIR(Diffuse, "Pluto_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Pluto_Normal"); SOLAR_TEXTURE_PAIR(Specular, "Pluto_Specular"); }

namespace Moon { SOLAR_TEXTURE_PAIR(Diffuse, "Moon_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Moon_Normal"); }
namespace Phobos { SOLAR_TEXTURE_PAIR(Diffuse, "Phobos_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Phobos_Normal"); }
namespace Deimos { SOLAR_TEXTURE_PAIR(Diffuse, "Deimos_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Deimos_Normal"); }
namespace Io { SOLAR_TEXTURE_PAIR(Diffuse, "Io_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Io_Normal"); }
namespace Europa { SOLAR_TEXTURE_PAIR(Diffuse, "Europa_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Europa_Normal"); }
namespace Ganymede { SOLAR_TEXTURE_PAIR(Diffuse, "Ganymede_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Ganymede_Normal"); }
namespace Callisto { SOLAR_TEXTURE_PAIR(Diffuse, "Callisto_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Callisto_Normal"); }
namespace Mimas { SOLAR_TEXTURE_PAIR(Diffuse, "Mimas_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Mimas_Normal"); }
namespace Enceladus { SOLAR_TEXTURE_PAIR(Diffuse, "Enceladus_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Enceladus_Normal"); }
namespace Tethys { SOLAR_TEXTURE_PAIR(Diffuse, "Tethys_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Tethys_Normal"); }
namespace Dione { SOLAR_TEXTURE_PAIR(Diffuse, "Dione_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Dione_Normal"); }
namespace Rhea { SOLAR_TEXTURE_PAIR(Diffuse, "Rhea_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Rhea_Normal"); }
namespace Titan { SOLAR_TEXTURE_PAIR(Diffuse, "Titan_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Titan_Normal"); }
namespace Iapetus { SOLAR_TEXTURE_PAIR(Diffuse, "Iapetus_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Iapetus_Normal"); }
namespace Miranda { SOLAR_TEXTURE_PAIR(Diffuse, "Miranda_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Miranda_Normal"); }
namespace Ariel { SOLAR_TEXTURE_PAIR(Diffuse, "Ariel_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Ariel_Normal"); }
namespace Umbriel { SOLAR_TEXTURE_PAIR(Diffuse, "Umbriel_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Umbriel_Normal"); }
namespace Titania { SOLAR_TEXTURE_PAIR(Diffuse, "Titania_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Titania_Normal"); }
namespace Oberon { SOLAR_TEXTURE_PAIR(Diffuse, "Oberon_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Oberon_Normal"); }
namespace Triton { SOLAR_TEXTURE_PAIR(Diffuse, "Triton_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Triton_Normal"); }
namespace Charon { SOLAR_TEXTURE_PAIR(Diffuse, "Charon_Diffuse"); SOLAR_TEXTURE_PAIR(Normal, "Charon_Normal"); SOLAR_TEXTURE_PAIR(Specular, "Charon_Specular"); }

#undef SOLAR_TEXTURE_PAIR
}
