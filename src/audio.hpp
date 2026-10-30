#pragma once
#include <gm/audio.hpp>
namespace gm {
const sdk::AudioService &audioService();
void pauseAudioForCurrentThread(bool paused);
} // namespace gm
