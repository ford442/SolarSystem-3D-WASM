#include "TextureLODController.h"
#include "TextureImage2D.h"
#include <glm/geometric.hpp>
#include <iostream>
#include <utility>

#if defined(__EMSCRIPTEN__) || defined(SOLARSYSTEM_BUILD_TESTS)
#define SOLARSYSTEM_TEXTURE_LOD_ENABLED 1
#else
#define SOLARSYSTEM_TEXTURE_LOD_ENABLED 0
#endif

void TextureLODController::Configure(TextureImage2D& texture,
                                     std::string lowPath,
                                     std::string midPath,
                                     std::string highPath,
                                     std::string label,
                                     TextureLoadCategory category) {
    _texture = &texture;
    _lowPath = std::move(lowPath);
    _midPath = std::move(midPath);
    _highPath = std::move(highPath);
    _label = std::move(label);
    _category = category;
    _residentTier = TextureLodTier::Low;
    _inFlightTier = TextureLodTier::Low;
    _inFlightPath.clear();
    _requestGeneration = 0;
}

const std::string& TextureLODController::PathForTier(TextureLodTier tier) const {
    switch (tier) {
        case TextureLodTier::Mid:
            return _midPath;
        case TextureLodTier::High:
            return _highPath;
        case TextureLodTier::Low:
        default:
            return _lowPath;
    }
}

std::string TextureLODController::TierSuffix(TextureLodTier tier) const {
    switch (tier) {
        case TextureLodTier::Mid: return "_Mid";
        case TextureLodTier::High: return "_High";
        case TextureLodTier::Low:
        default: return "_Low";
    }
}

bool TextureLODController::HasPath(TextureLodTier tier) const {
    return !PathForTier(tier).empty();
}

TextureLodTier TextureLODController::NextUpgradeTier(TextureLodTier resident, TextureLodTier allowedMax) const {
    if (resident >= allowedMax) {
        return TextureLodTier::Low;
    }
    if (resident == TextureLodTier::Low) {
        if (allowedMax >= TextureLodTier::Mid && HasPath(TextureLodTier::Mid)) {
            return TextureLodTier::Mid;
        }
        if (allowedMax >= TextureLodTier::High && HasPath(TextureLodTier::High)) {
            return TextureLodTier::High;
        }
        return TextureLodTier::Low;
    }
    if (resident == TextureLodTier::Mid && allowedMax >= TextureLodTier::High && HasPath(TextureLodTier::High)) {
        return TextureLodTier::High;
    }
    return TextureLodTier::Low;
}

void TextureLODController::ApplyTier(TextureLodTier tier) {
    if (!_texture || !HasPath(tier)) {
        return;
    }
    _texture->ReloadTexture(PathForTier(tier));
    _residentTier = tier;
}

void TextureLODController::CancelInFlight() {
    if (_inFlightTier == TextureLodTier::Low) {
        return;
    }
    ++_requestGeneration;
    if (!_inFlightPath.empty()) {
        TextureLoadingQueue::GetInstance().CancelLoad(_inFlightPath);
    }
    std::cout << "[LOD][" << _label << "] Cancelled " << TextureLodTierName(_inFlightTier)
              << " upgrade" << std::endl;
    _inFlightTier = TextureLodTier::Low;
    _inFlightPath.clear();
}

void TextureLODController::QueueTier(TextureLodTier tier) {
    if (!_texture || !HasPath(tier) || _inFlightTier != TextureLodTier::Low) {
        return;
    }

    const std::string& path = PathForTier(tier);
    const std::uint64_t generation = ++_requestGeneration;
    const bool queued = TextureLoadingQueue::GetInstance().QueueTextureLoad(
        path, _label + TierSuffix(tier), _texture,
        [this, generation, tier](bool success) {
            if (generation != _requestGeneration) {
                return;
            }
            _inFlightTier = TextureLodTier::Low;
            _inFlightPath.clear();
            if (success) {
                _residentTier = tier;
                std::cout << "[LOD][" << _label << "] Resident tier now "
                          << TextureLodTierName(tier) << std::endl;
            } else {
                std::cout << "[LOD][" << _label << "] Failed " << TextureLodTierName(tier)
                          << " upgrade (keeping " << TextureLodTierName(_residentTier) << ")"
                          << std::endl;
            }
        },
        _category);

    if (queued) {
        _inFlightTier = tier;
        _inFlightPath = path;
        std::cout << "[LOD][" << _label << "] Queueing " << TextureLodTierName(tier)
                  << " texture" << std::endl;
    }
}

void TextureLODController::Update(const glm::vec3& cameraPosition, const glm::vec3& objectPosition, float threshold) {
#if SOLARSYSTEM_TEXTURE_LOD_ENABLED
    if (!_texture) {
        return;
    }

    // Prefer mid/high path when either is configured; pure low-only textures are static.
    if (_midPath.empty() && _highPath.empty()) {
        return;
    }

    const TextureLodTier allowedMax = GetMaxTextureLodTier();
    const float distance = glm::distance(cameraPosition, objectPosition);

    // Cap resident tier when quality preset drops (e.g. full → medium/low).
    if (_residentTier > allowedMax) {
        CancelInFlight();
        TextureLodTier target = allowedMax;
        while (target > TextureLodTier::Low && !HasPath(target)) {
            target = static_cast<TextureLodTier>(static_cast<uint8_t>(target) - 1);
        }
        ApplyTier(target);
        std::cout << "[LOD][" << _label << "] Capped to " << TextureLodTierName(target)
                  << " by quality preset" << std::endl;
        return;
    }

    if (_inFlightTier != TextureLodTier::Low) {
        const bool cancelHigh = _inFlightTier == TextureLodTier::High &&
                                (distance > threshold * 0.9f || allowedMax < TextureLodTier::High);
        const bool cancelMid = _inFlightTier == TextureLodTier::Mid &&
                               (distance > threshold * 1.8f || allowedMax < TextureLodTier::Mid);
        if (cancelHigh || cancelMid) {
            CancelInFlight();
        }
        return;
    }

    // Downgrade with hysteresis (high → mid → low).
    if (_residentTier == TextureLodTier::High &&
        (distance > threshold || allowedMax < TextureLodTier::High)) {
        if (HasPath(TextureLodTier::Mid) && allowedMax >= TextureLodTier::Mid) {
            ApplyTier(TextureLodTier::Mid);
            std::cout << "[LOD][" << _label << "] Downgraded high → mid" << std::endl;
        } else {
            ApplyTier(TextureLodTier::Low);
            std::cout << "[LOD][" << _label << "] Downgraded high → low" << std::endl;
        }
        return;
    }

    if (_residentTier == TextureLodTier::Mid &&
        (distance > threshold * 2.0f || allowedMax < TextureLodTier::Mid)) {
        ApplyTier(TextureLodTier::Low);
        std::cout << "[LOD][" << _label << "] Downgraded mid → low" << std::endl;
        return;
    }

    // Upgrades: mid when close, high when very close (full preset only).
    const TextureLodTier next = NextUpgradeTier(_residentTier, allowedMax);
    if (next == TextureLodTier::Low) {
        return;
    }

    if (next == TextureLodTier::Mid && distance < threshold) {
        QueueTier(TextureLodTier::Mid);
        return;
    }

    if (next == TextureLodTier::High && distance < threshold * 0.5f) {
        QueueTier(TextureLodTier::High);
    }
#else
    (void)cameraPosition;
    (void)objectPosition;
    (void)threshold;
#endif
}

#ifdef SOLARSYSTEM_BUILD_TESTS
void TextureLODController::ResetForTests() {
    _texture = nullptr;
    _lowPath.clear();
    _midPath.clear();
    _highPath.clear();
    _label.clear();
    _category = TextureLoadCategory::Planet;
    _residentTier = TextureLodTier::Low;
    _inFlightTier = TextureLodTier::Low;
    _inFlightPath.clear();
    _requestGeneration = 0;
}
#endif
