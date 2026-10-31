#pragma once
#include <cstdint>
namespace gm::editorTheme {
// Colors are RGBA packed for the existing bgfx 2D batcher.
constexpr uint32_t rgb(uint32_t c) {
    return ((c >> 16) & 255) | (c & 0xff00) | ((c & 255) << 16) | 0xff000000;
}
inline constexpr auto background = rgb(0x15171C), panel = rgb(0x21242B), header = rgb(0x272B33),
                      raised = rgb(0x2C313B), field = rgb(0x191C22), border = rgb(0x3D4653),
                      divider = rgb(0x15181D), text = rgb(0xE2E7EE), muted = rgb(0xA4AEBD),
                      disabled = rgb(0x667080), accent = rgb(0x82B4FA), selection = rgb(0x2E4461),
                      hover = rgb(0x333B48), green = rgb(0x98DBB1), amber = rgb(0xF1CB81),
                      red = rgb(0xF19A9A);
inline constexpr float menuHeight = 30, toolbarHeight = 48, panelHeader = 32, row = 28;
} // namespace gm::editorTheme
