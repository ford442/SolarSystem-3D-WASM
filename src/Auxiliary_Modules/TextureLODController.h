#pragma once

#include "TextureLoadingQueue.h"
#include <glm/vec3.hpp>
#include <cstdint>
#include <string>

class TextureImage2D;

class TextureLODController {
public:
    void Configure(TextureImage2D& texture, std::string lowPath, std::string highPath,
                   std::string label, TextureLoadCategory category);
    void Update(const glm::vec3& cameraPosition, const glm::vec3& objectPosition, float threshold = 50.0f);

private:
    TextureImage2D* _texture = nullptr;
    std::string _lowPath;
    std::string _highPath;
    std::string _label;
    TextureLoadCategory _category = TextureLoadCategory::Planet;
    bool _isHighResLoaded = false;
    bool _isHighResLoading = false;
    std::uint64_t _requestGeneration = 0;
};
