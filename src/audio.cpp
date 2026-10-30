#include "audio.hpp"
#include <miniaudio.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
namespace gm {
namespace {
using namespace sdk;
struct Session;
thread_local std::vector<Session *> sessions;
void check(bool ok, const char *text) {
    if (!ok)
        throw std::runtime_error(text);
}
void result(ma_result r, const char *text) {
    if (r != MA_SUCCESS)
        throw std::runtime_error(std::string(text) + ": " + ma_result_description(r));
}
struct Sound {
    ma_sound value{};
    bool initialized = false, stream = false, paused = false;
    uint64_t clip = 0, decodedBytes = 0;
    std::filesystem::path path;
    ~Sound() {
        if (initialized)
            ma_sound_uninit(&value);
    }
};
struct Session {
    ma_engine engine{};
    ma_sound_group groups[3]{};
    unsigned groupCount = 0;
    bool initialized = false, headless = false, suspended = false, appPaused = false;
    std::thread::id thread = std::this_thread::get_id();
    std::map<uint64_t, std::unique_ptr<Sound>> clips, voices;
    uint64_t next = 1;
    uint64_t decodedBytes = 0;
    ~Session() {
        std::erase(sessions, this);
        voices.clear();
        clips.clear();
        while (groupCount)
            ma_sound_group_uninit(&groups[--groupCount]);
        if (initialized)
            ma_engine_uninit(&engine);
    }
    void owner() {
        check(thread == std::this_thread::get_id(), "Audio session used from a different thread");
    }
    void init(const AudioConfig &c) {
        check(c.structSize >= sizeof(c) && c.sampleRate >= 8000 && c.sampleRate <= 192000,
              "Invalid audio config");
        headless = c.headless != 0;
        auto config = ma_engine_config_init();
        config.channels = 2;
        config.sampleRate = c.sampleRate;
        config.noDevice = headless ? MA_TRUE : MA_FALSE;
        result(ma_engine_init(&config, &engine), "Cannot initialize audio device");
        initialized = true;
        for (auto &group : groups) {
            result(ma_sound_group_init(&engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &group),
                   "Cannot create audio bus");
            ++groupCount;
        }
        sessions.push_back(this);
    }
    void file(Sound &sound, ma_sound_group *group) {
        const ma_uint32 flags =
            MA_SOUND_FLAG_NO_SPATIALIZATION | (sound.stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE);
#ifdef _WIN32
        result(ma_sound_init_from_file_w(&engine, sound.path.c_str(), flags, group, nullptr, &sound.value),
               "Cannot load sound");
#else
        result(ma_sound_init_from_file(&engine, sound.path.c_str(), flags, group, nullptr, &sound.value),
               "Cannot load sound");
#endif
        sound.initialized = true;
    }
    AudioHandle load(const char *path, bool stream) {
        owner();
        check(path && *path && clips.size() < 128, "Missing sound path or clip limit reached");
        auto sound = std::make_unique<Sound>();
        std::string text(path);
        sound->path = std::filesystem::path(std::u8string(text.begin(), text.end()));
        sound->stream = stream;
        check(std::filesystem::is_regular_file(sound->path), "Audio file not found");
        // Bound decoded effects before the resource manager allocates their PCM buffer.
        if (!stream) {
            ma_decoder decoder{};
            ma_decoder_config config =
                ma_decoder_config_init(ma_format_f32, 0, ma_engine_get_sample_rate(&engine));
#ifdef _WIN32
            auto status = ma_decoder_init_file_w(sound->path.c_str(), &config, &decoder);
#else
            auto status = ma_decoder_init_file(sound->path.c_str(), &config, &decoder);
#endif
            result(status, "Cannot inspect audio clip");
            ma_uint64 frames = 0;
            ma_uint32 channels = decoder.outputChannels;
            status = ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
            ma_decoder_uninit(&decoder);
            check(status == MA_SUCCESS && frames && channels &&
                      frames <= (64 * 1024 * 1024) / (uint64_t(channels) * sizeof(float)),
                  "Decoded audio exceeds 64 MiB; use streaming");
            sound->decodedBytes = frames * channels * sizeof(float);
            check(decodedBytes + sound->decodedBytes <= 128 * 1024 * 1024,
                  "Decoded audio session budget exceeds 128 MiB");
        }
        file(*sound, nullptr);
        auto id = next++;
        auto bytes = sound->decodedBytes;
        clips.emplace(id, std::move(sound));
        decodedBytes += bytes;
        return id;
    }
    Sound &voice(AudioHandle id) {
        auto it = voices.find(id);
        check(it != voices.end(), "Unknown audio voice");
        return *it->second;
    }
    void unload(AudioHandle id) {
        owner();
        check(clips.count(id) != 0, "Unknown audio clip");
        for (auto &[key, v] : voices)
            check(v->clip != id, "Audio clip still has voices; stop or collect them first");
        decodedBytes -= clips.at(id)->decodedBytes;
        clips.erase(id);
    }
    AudioHandle play(AudioHandle clip, AudioBus bus, bool loop, float volume) {
        owner();
        check(uint32_t(bus) <= 3 && std::isfinite(volume) && volume >= 0 && volume <= 1,
              "Invalid audio bus/volume");
        collect();
        check(voices.size() < 128, "Audio voice limit reached");
        auto it = clips.find(clip);
        check(it != clips.end(), "Unknown audio clip");
        auto sound = std::make_unique<Sound>();
        sound->clip = clip;
        sound->stream = it->second->stream;
        sound->path = it->second->path;
        auto *group = bus == AudioBus::Master ? nullptr : &groups[uint32_t(bus) - 1];
        if (sound->stream)
            file(*sound, group);
        else {
            result(ma_sound_init_copy(&engine, &it->second->value, MA_SOUND_FLAG_NO_SPATIALIZATION, group,
                                      &sound->value),
                   "Cannot create audio voice");
            sound->initialized = true;
        }
        ma_sound_set_looping(&sound->value, loop ? MA_TRUE : MA_FALSE);
        ma_sound_set_volume(&sound->value, volume);
        result(ma_sound_start(&sound->value), "Cannot start sound");
        auto id = next++;
        voices.emplace(id, std::move(sound));
        return id;
    }
    AudioStats collect() {
        owner();
        std::erase_if(voices,
                      [](const auto &v) { return !v.second->paused && ma_sound_at_end(&v.second->value); });
        return {uint32_t(clips.size()), uint32_t(voices.size())};
    }
};
Session &get(void *p) {
    check(p != nullptr, "Missing audio session");
    return *static_cast<Session *>(p);
}
template <class F> bool call(Error *e, F &&fn) {
    if (e)
        *e = {};
    try {
        fn();
        return true;
    } catch (const std::exception &x) {
        if (e)
            std::strncpy(e->message, x.what(), 511);
        return false;
    } catch (...) {
        if (e)
            std::strcpy(e->message, "Audio operation failed");
        return false;
    }
}
} // namespace
const sdk::AudioService &audioService() {
    static const sdk::AudioService service{
        sizeof(sdk::AudioService),
        1,
        [](const AudioConfig *c, Error *e) -> void * {
            Session *out = nullptr;
            call(e, [&] {
                check(c != nullptr, "Missing audio config");
                auto s = std::make_unique<Session>();
                s->init(*c);
                out = s.release();
            });
            return out;
        },
        [](void *p) { delete static_cast<Session *>(p); },
        [](void *p, const char *path, bool stream, Error *e) {
            AudioHandle id = 0;
            call(e, [&] { id = get(p).load(path, stream); });
            return id;
        },
        [](void *p, AudioHandle id, Error *e) { return call(e, [&] { get(p).unload(id); }); },
        [](void *p, AudioHandle clip, AudioBus b, bool loop, float v, Error *e) {
            AudioHandle id = 0;
            call(e, [&] { id = get(p).play(clip, b, loop, v); });
            return id;
        },
        [](void *p, AudioHandle id, Error *e) {
            return call(e, [&] {
                auto &s = get(p);
                s.owner();
                s.voice(id);
                s.voices.erase(id);
            });
        },
        [](void *p, AudioHandle id, bool paused, Error *e) {
            return call(e, [&] {
                auto &s = get(p);
                s.owner();
                auto &v = s.voice(id);
                result(paused ? ma_sound_stop(&v.value) : ma_sound_start(&v.value),
                       "Cannot pause/resume sound");
                v.paused = paused;
            });
        },
        [](void *p, AudioBus bus, float volume, Error *e) {
            return call(e, [&] {
                auto &s = get(p);
                s.owner();
                check(uint32_t(bus) <= 3 && std::isfinite(volume) && volume >= 0 && volume <= 1,
                      "Invalid audio volume");
                if (bus == AudioBus::Master)
                    result(ma_engine_set_volume(&s.engine, volume), "Cannot set master volume");
                else
                    ma_sound_group_set_volume(&s.groups[uint32_t(bus) - 1], volume);
            });
        },
        [](void *p, bool suspended, Error *e) {
            return call(e, [&] {
                auto &s = get(p);
                s.owner();
                if (!s.headless && !s.appPaused && s.suspended != suspended)
                    result(suspended ? ma_engine_stop(&s.engine) : ma_engine_start(&s.engine),
                           "Cannot suspend/resume audio");
                s.suspended = suspended;
            });
        },
        [](void *p, AudioHandle id) {
            try {
                auto &s = get(p);
                s.owner();
                return !s.suspended && !s.appPaused && ma_sound_is_playing(&s.voice(id).value) != 0;
            } catch (...) {
                return false;
            }
        },
        [](void *p) {
            try {
                return get(p).collect();
            } catch (...) {
                return AudioStats{};
            }
        },
        [](void *p, float *samples, uint32_t frames, Error *e) {
            return call(e, [&] {
                auto &s = get(p);
                s.owner();
                check(s.headless && samples && frames <= 192000, "Invalid offline audio buffer");
                if (s.suspended || s.appPaused)
                    std::fill(samples, samples + size_t(frames) * 2, 0.f);
                else
                    result(ma_engine_read_pcm_frames(&s.engine, samples, frames, nullptr),
                           "Cannot render audio");
            });
        }};
    return service;
}
void pauseAudioForCurrentThread(bool paused) {
    for (auto *s : sessions) {
        if (!s->headless && !s->suspended && s->appPaused != paused)
            result(paused ? ma_engine_stop(&s->engine) : ma_engine_start(&s->engine),
                   "Cannot change app audio lifecycle");
        s->appPaused = paused;
    }
}
} // namespace gm
