#pragma once
#include "scene.hpp"
#include <windows.h>
#include <Xinput.h>
#include <gm/input.hpp>
#include <algorithm>

namespace gm {
// Shared by Player and the editor's game viewport. Message edges survive short clicks;
// polling supplies continuous keys. Clear on focus loss/capture cancellation.
class WindowsGameInput {
    sdk::Input state_{};
    sdk::TouchInput touches_;
public:
    void clear() { state_ = {};touches_.clear(); }
    void touch(int32_t id,float x,float y,bool down){touches_.set(id,x,y,down);}
    bool pointer(HWND window,WPARAM param,bool down){POINTER_INFO info{};if(!GetPointerInfo(GET_POINTERID_WPARAM(param),&info)||info.pointerType!=PT_TOUCH)return false;auto p=info.ptPixelLocation;ScreenToClient(window,&p);touch(int32_t(info.pointerId),float(p.x),float(p.y),down&&(info.pointerFlags&POINTER_FLAG_INCONTACT)&&!(info.pointerFlags&POINTER_FLAG_CANCELED));return true;}
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
        touches_.sample(state_);
        for(uint32_t i=0;i<state_.touchCount;++i){auto &t=state_.touches[i];t.x=(t.x-viewport.x-(viewport.w-width*scale)*.5f)/scale;t.y=(t.y-viewport.y-(viewport.h-height*scale)*.5f)/scale;}
        using Poll=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
        static auto poll=[]()->Poll{auto library=LoadLibraryW(L"xinput1_4.dll");return library?reinterpret_cast<Poll>(GetProcAddress(library,"XInputGetState")):nullptr;}();
        for(DWORD i=0;i<4;++i){auto &pad=state_.gamepads[i];pad={};XINPUT_STATE raw{};if(poll&&poll(i,&raw)==ERROR_SUCCESS){pad.connected=1;pad.buttons=raw.Gamepad.wButtons;pad.axes[0]=std::max(-1.f,raw.Gamepad.sThumbLX/32767.f);pad.axes[1]=-std::max(-1.f,raw.Gamepad.sThumbLY/32767.f);pad.axes[2]=std::max(-1.f,raw.Gamepad.sThumbRX/32767.f);pad.axes[3]=-std::max(-1.f,raw.Gamepad.sThumbRY/32767.f);pad.axes[4]=raw.Gamepad.bLeftTrigger/255.f;pad.axes[5]=raw.Gamepad.bRightTrigger/255.f;}}
        auto result=state_;
        state_.pressed=state_.released=0;state_.wheel=0;
        for(auto& key:state_.keyPress) key=0;
        return result;
    }
};
}
