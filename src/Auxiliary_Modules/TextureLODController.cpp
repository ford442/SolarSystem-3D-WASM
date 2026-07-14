#include "TextureLODController.h"
#include "TextureImage2D.h"
#include <glm/geometric.hpp>
#include <iostream>
#include <utility>

#ifdef __EMSCRIPTEN__
extern int g_qualityPreset;
#endif

void TextureLODController::Configure(TextureImage2D& texture, std::string lowPath, std::string highPath,
                                     std::string label, TextureLoadCategory category) {
    _texture = &texture;
    _lowPath = std::move(lowPath);
    _highPath = std::move(highPath);
    _label = std::move(label);
    _category = category;
}

void TextureLODController::Update(const glm::vec3& cameraPosition, const glm::vec3& objectPosition, float threshold) {
#ifdef __EMSCRIPTEN__
    if (!_texture || _highPath.empty()) return;

    const float effectiveThreshold = g_qualityPreset == 1 ? threshold / 1.5f : threshold;
    const float distance = glm::distance(cameraPosition, objectPosition);

    if (_isHighResLoaded && (g_qualityPreset == 0 || distance > effectiveThreshold * 2.0f)) {
        _texture->ReloadTexture(_lowPath);
        _isHighResLoaded = false;
        std::cout << "[LOD][" << _label << "] Downgraded diffuse texture" << std::endl;
    }

    if (_isHighResLoading && (g_qualityPreset == 0 || distance > effectiveThreshold * 1.8f)) {
        ++_requestGeneration;
        TextureLoadingQueue::GetInstance().CancelLoad(_highPath);
        _isHighResLoading = false;
        std::cout << "[LOD][" << _label << "] Cancelled distant diffuse upgrade" << std::endl;
    }

    if (g_qualityPreset == 0 || distance >= effectiveThreshold || _isHighResLoaded || _isHighResLoading) return;

    const std::uint64_t generation = ++_requestGeneration;
    const bool queued = TextureLoadingQueue::GetInstance().QueueTextureLoad(
        _highPath, _label + "_Diffuse_High", _texture,
        [this, generation](bool success) {
            if (generation != _requestGeneration) return;
            _isHighResLoading = false;
            _isHighResLoaded = success;
        },
        _category);

    if (queued) {
        _isHighResLoading = true;
        std::cout << "[LOD][" << _label << "] Queueing high-res diffuse texture" << std::endl;
    }
#else
    (void)cameraPosition;
    (void)objectPosition;
    (void)threshold;
#endif
}
