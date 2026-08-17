#pragma once

#include "TextureLoadingQueue.h"
#include "../QualitySettings.h"
#include <glm/vec3.hpp>
#include <cstdint>
#include <string>

class TextureImage2D;

class TextureLODController {
public:
    void Configure(TextureImage2D& texture,
                   std::string lowPath,
                   std::string midPath,
                   std::string highPath,
                   std::string label,
                   TextureLoadCategory category);

    void Update(const glm::vec3& cameraPosition, const glm::vec3& objectPosition, float threshold = 50.0f);

    TextureLodTier GetResidentTier() const { return _residentTier; }
    TextureLodTier GetInFlightTier() const { return _inFlightTier; }
    bool IsLoading() const { return _inFlightTier != TextureLodTier::Low; }

#ifdef SOLARSYSTEM_BUILD_TESTS
    void ResetForTests();
#endif

private:
    const std::string& PathForTier(TextureLodTier tier) const;
    std::string TierSuffix(TextureLodTier tier) const;
    bool HasPath(TextureLodTier tier) const;
    TextureLodTier NextUpgradeTier(TextureLodTier resident, TextureLodTier allowedMax) const;
    void ApplyTier(TextureLodTier tier);
    void CancelInFlight();
    void QueueTier(TextureLodTier tier);

    TextureImage2D* _texture = nullptr;
    std::string _lowPath;
    std::string _midPath;
    std::string _highPath;
    std::string _label;
    TextureLoadCategory _category = TextureLoadCategory::Planet;
    TextureLodTier _residentTier = TextureLodTier::Low;
    TextureLodTier _inFlightTier = TextureLodTier::Low;
    std::string _inFlightPath;
    std::uint64_t _requestGeneration = 0;
};
