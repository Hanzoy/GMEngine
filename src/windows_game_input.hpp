#pragma once
#include "scene.hpp"
#include <windows.h>
#include <algorithm>

namespace gm {
// Shared by Player and the editor's game viewport. Message edges survive short clicks;
// polling supplies continuous keys. Clear on focus loss/capture cancellation.
class WindowsGameInput {
    sdk::Input state_{};
public:
    void clear() { state_ = {}; }
    void cancelButtons() { state_.released |= state_.buttons;state_.buttons=0; }
    void button(uint32_t bit, bool down) {
        if(down) { state_.buttons |= bit; state_.pressed |= bit; }
        else { state_.buttons &= ~bit; state_.released |= bit; }
    }
    void key(uint32_t key, bool repeat) {
        if(key < 256 && !repeat) state_.keyPress[key/32] |= 1u << (key%32);
    }
    void wheel(float amount) { state_.wheel += amount; }
    sdk::Input sample(HWND window, Rect viewport, float width, float height, bool focused) {
        if(!focused || viewport.w <= 0 || viewport.h <= 0) { clear(); return state_; }
        POINT mouse; GetCursorPos(&mouse); ScreenToClient(window,&mouse);
        float scale=std::min(viewport.w/width,viewport.h/height);
        state_.mouseX=(mouse.x-viewport.x-(viewport.w-width*scale)*.5f)/scale;
        state_.mouseY=(mouse.y-viewport.y-(viewport.h-height*scale)*.5f)/scale;
        state_.focused=1;
        for(uint32_t k=0;k<256;++k) {
            auto mask=1u << (k%32);
            if(GetAsyncKeyState(int(k))&0x8000) state_.keys[k/32] |= mask;
            else state_.keys[k/32] &= ~mask;
        }
        auto result=state_;
        state_.pressed=state_.released=0;state_.wheel=0;
        for(auto& key:state_.keyPress) key=0;
        return result;
    }
};
}
