#pragma once
#include "extensions.hpp"
namespace gm::sdk {
enum class AudioBus : uint32_t { Master, Effects, Music, UI };
using AudioHandle = uint64_t;
struct AudioConfig {
    uint32_t structSize = sizeof(AudioConfig), sampleRate = 48000, headless = 0;
};
struct AudioStats {
    uint32_t clips = 0, voices = 0;
};
struct AudioService {
    static constexpr TypeId Id = id("gm.Audio");
    static constexpr uint32_t Version = 1;
    uint32_t structSize, version;
    void *(*open)(const AudioConfig *, Error *);
    void (*close)(void *);
    // Owner-thread loading: decoded effects share buffers; stream long music.
    // UTF-8 filesystem path. Load during loading screens, not the audio callback.
    AudioHandle (*load)(void *, const char *file, bool stream, Error *);
    bool (*unload)(void *, AudioHandle, Error *);
    AudioHandle (*play)(void *, AudioHandle clip, AudioBus, bool loop, float volume, Error *);
    bool (*stop)(void *, AudioHandle voice, Error *);
    bool (*pause)(void *, AudioHandle voice, bool paused, Error *);
    bool (*volume)(void *, AudioBus, float, Error *);
    bool (*suspend)(void *, bool, Error *);
    bool (*playing)(void *, AudioHandle);
    // Reclaims completed voices. Paused voices stay alive until stop/close.
    AudioStats (*collect)(void *);
    // Headless sessions only: stereo f32, capacity=frames*2. Tests/offline capture.
    bool (*render)(void *, float *samples, uint32_t frames, Error *);
};
} // namespace gm::sdk
