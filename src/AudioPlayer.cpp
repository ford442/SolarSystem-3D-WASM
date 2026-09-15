// Background music playback and volume control. Two backends live behind the same
// Application methods: SDL_mixer (web/Emscripten, pumped once per frame from the main
// loop) and irrKlang (Windows native, driven by its own worker thread).
#include "Application.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>

using namespace std;

void Application::SetMusicVolume(float volume) {
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    _musicVolume = glm::clamp(volume, 0.0f, 1.0f);
    if (!_musicMuted && _mixerInitialized && Mix_PlayingMusic()) {
        Mix_VolumeMusic(static_cast<int>(MIX_MAX_VOLUME * _musicVolume));
    }
#else
    _soundEngine->setSoundVolume(glm::clamp(volume, 0.0f, 1.0f));
#endif
}

float Application::GetMusicVolume() const {
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    return _musicVolume;
#else
    return _soundEngine->getSoundVolume();
#endif
}

void Application::SetMusicMuted(bool muted) {
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    _musicMuted = muted;
    if (!_mixerInitialized) {
        return;
    }
    if (_musicMuted) {
        Mix_VolumeMusic(0);
        return;
    }
    if (Mix_PlayingMusic()) {
        Mix_VolumeMusic(static_cast<int>(MIX_MAX_VOLUME * _musicVolume));
    }
#else
    (void)muted;
#endif
}

bool Application::GetMusicMuted() const {
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    return _musicMuted;
#else
    return false;
#endif
}

void Application::StartPlayBackgroundMusic() {
    _isBackgroundMusicPlay = true;

#ifdef SOLARSYSTEM_USE_SDL_MIXER
    _currentSongIndex = 0;
    _musicStartTime = 0;
    _musicDuration = 0;
    _currentMusic = nullptr;
#else
    auto mapRange = [](float value, float inMin, float inMax, float outMin, float outMax) {
        return outMin + (outMax - outMin) * (value - inMin) / (inMax - inMin);
    };

    _backgroundMusicThread = make_unique<thread>([=]() {
        for (ssize_t i = 0; i < _backgroundSongPaths.size() && _isBackgroundMusicPlay; i++) {
            this_thread::sleep_for(1s);

            auto song = _soundEngine->play2D(_backgroundSongPaths[i].c_str(), false, true, true);
            song->setVolume(0);
            song->setIsPaused(false);
            _currentMusicTrack = _backgroundSongPaths[i].substr(16);

            while (!song->isFinished()) {
                if (song->getPlayPosition() < 5000) {
                    auto x = mapRange(song->getPlayPosition(), 0.0f, 5000.0f, 0.0f, 0.7f);
                    auto volume = exp(x) - 1;
                    song->setVolume(clamp(volume, 0.0f, 1.0f));
                }
                else if (song->getPlayPosition() > song->getPlayLength() - 5000) {
                    auto x = mapRange(song->getPlayPosition(), song->getPlayLength() - 5000, song->getPlayLength(), 0.0f, 6.0f);
                    auto volume = exp(-x);
                    song->setVolume(clamp(volume, 0.0f, 1.0f));
                }

                this_thread::sleep_for(25ms);
            }

            if (i == _backgroundSongPaths.size() - 1)
                i = -1;

            song->drop();
        }
    });
#endif
}

void Application::UpdateBackgroundMusic() {
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    if (!_isBackgroundMusicPlay || !_mixerInitialized || _musicMuted || _backgroundSongPaths.empty())
        return;

    if (_availableSongPaths.empty())
        return;

    const auto applyMusicVolume = [this](float fadeMultiplier) {
        Mix_VolumeMusic(static_cast<int>(MIX_MAX_VOLUME * _musicVolume * fadeMultiplier));
    };

    if (!Mix_PlayingMusic()) {
        if (_currentMusic) {
            Mix_FreeMusic(_currentMusic);
            _currentMusic = nullptr;
        }

        const size_t songCount = _backgroundSongPaths.size();
        for (size_t attempt = 0; attempt < songCount; ++attempt) {
            if (_currentSongIndex >= static_cast<int>(songCount)) {
                _currentSongIndex = 0;
            }

            const std::string& path = _backgroundSongPaths[_currentSongIndex];
            ++_currentSongIndex;

            if (_availableSongPaths.count(path) == 0) {
                continue;
            }

            _currentMusic = Mix_LoadMUS(path.c_str());
            if (!_currentMusic) {
                std::cerr << "[Audio] Failed to decode track (skipped): " << path
                          << " (" << Mix_GetError() << ")" << std::endl;
                _availableSongPaths.erase(path);
                continue;
            }

            _currentMusicTrack = path.substr(std::string("resource/sounds/").size());
            Mix_PlayMusic(_currentMusic, 1);
            applyMusicVolume(0.0f);
            _musicStartTime = SDL_GetTicks();
            _musicDuration = 180000;
            std::cout << "[Audio] Now playing: " << _currentMusicTrack << std::endl;
            return;
        }

        std::cerr << "[Audio] No playable tracks available yet; waiting for downloads" << std::endl;
        return;
    }

    const uint32_t currentTime = SDL_GetTicks();
    const uint32_t elapsed = currentTime - _musicStartTime;

    if (elapsed < 5000) {
        applyMusicVolume(static_cast<float>(elapsed) / 5000.0f);
    } else if (_musicDuration > 0 && elapsed > _musicDuration - 5000) {
        applyMusicVolume(static_cast<float>(_musicDuration - elapsed) / 5000.0f);
    } else {
        applyMusicVolume(1.0f);
    }
#endif
}

void Application::StopPlayBackgroundMusic() {
    _isBackgroundMusicPlay = false;
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    if (_currentMusic) {
        Mix_HaltMusic();
        Mix_FreeMusic(_currentMusic);
        _currentMusic = nullptr;
    }
#else
    _soundEngine->stopAllSounds();
    if (_backgroundMusicThread)
        _backgroundMusicThread->join();
#endif
}

void Application::UpdateMusicDucking() {
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    if (!_mixerInitialized || _musicMuted || !Mix_PlayingMusic()) {
        return;
    }
    float duck = 1.0f;
    if (_nearestPlanetIndex >= 0
        && static_cast<size_t>(_nearestPlanetIndex) < _renderableSceneComponents.size()) {
        const Planet* planet = _renderableSceneComponents[static_cast<size_t>(_nearestPlanetIndex)].planet.get();
        if (planet) {
            const float dist = CalculateSpaceObjectDistance(planet);
            constexpr float kDuckStart = 90.0f;
            constexpr float kDuckEnd = 18.0f;
            if (dist < kDuckStart) {
                const float t = glm::clamp((dist - kDuckEnd) / (kDuckStart - kDuckEnd), 0.0f, 1.0f);
                duck = glm::mix(0.32f, 1.0f, t);
            }
        }
    }
    Mix_VolumeMusic(static_cast<int>(MIX_MAX_VOLUME * _musicVolume * duck));
#else
    (void)0;
#endif
}
