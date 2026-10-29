#pragma once
#include "image_types.hpp"
#include <filesystem>
#include <span>
#include <vector>

namespace gm {
inline constexpr uint32_t NoIndex=UINT32_MAX;
enum class UiKind:uint32_t {Panel,Label,Button,Image};
enum class UiOp:uint32_t {AddInteger,SetText,SetColor,SetTextColor,SetVisible,SetX,SetY,SetWidth,SetHeight,SetFontSize,SetImageFit,SetTint,SetOpacity};
struct UiNode {
    UiKind kind=UiKind::Panel;
    uint32_t parent=NoIndex,subtreeEnd=0,action=NoIndex;
    Rect bounds{};
    uint32_t color=0xff382518,textColor=0xfff2e7df;
    float fontSize=14;
    bool visible=true,integerText=false;
    int64_t integerValue=0;
    std::string text;
    uint32_t image=NoIndex,tint=0xffffffff;
    ImageFit fit=ImageFit::Contain;
};
struct UiAction {
    UiOp op=UiOp::SetText;
    uint32_t target=0;
    float number=0;
    uint32_t packed=0;
    int64_t integer=0;
    std::string text;
};
struct CompiledUi {std::vector<UiNode> nodes;std::vector<UiAction> actions;std::vector<UiImage> images;};
bool parseCounter(const std::string& text,int64_t& value);
void validateCompiledUi(const CompiledUi& asset);
std::vector<uint8_t> encodeUi(const CompiledUi& asset);
CompiledUi decodeUi(std::span<const uint8_t> bytes);
CompiledUi loadUi(const std::filesystem::path& path);
void saveUi(const std::filesystem::path& path,const CompiledUi& asset);
}
