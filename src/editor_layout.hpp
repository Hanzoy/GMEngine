#pragma once
#include "document.hpp"
#include <algorithm>
#include <map>
namespace gm {
struct WorkspaceLayout {
    float left = 230, right = 330, treeFraction = 0.58f, outputHeight = 180;
    bool outputOpen = false;
    std::map<std::string,Rect> regions;
    Rect leftSplit{}, rightSplit{}, treeSplit{}, outputSplit{};
    void arrange(float width, float height) {
        left=std::clamp(left,180.f,std::min(340.f,width*.27f));
        right=std::clamp(right,260.f,std::min(400.f,width*.32f));
        treeFraction=std::clamp(treeFraction,.28f,.75f);
        const float top=66, bottom=height-22, body=bottom-top, gap=4;
        const float centerX=left+gap, centerW=width-left-right-2*gap;
        outputHeight=std::clamp(outputHeight,180.f,std::max(180.f,body*.48f));
        const float outputH=outputHeight;
        regions={
            {"menubar",{0,0,width,30}},
            {"toolbar",{0,30,width,36}},
            {"left-top",{0,top,left,body-outputH-gap}},
            {"center",{centerX,top,centerW,body-outputH-gap}},
            {"right",{width-right,top,right,body}},
            {"bottom",{0,bottom-outputH,width-right-gap,outputH}}, {"status",{0,bottom,width,22}}
        };
        leftSplit={left,top,gap,body-outputH-gap}; rightSplit={width-right-gap,top,gap,body};
        treeSplit={}; outputSplit={0,bottom-outputH-gap,width-right-gap,gap};
    }
};
}
