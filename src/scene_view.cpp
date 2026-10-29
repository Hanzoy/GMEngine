#include "scene_view.hpp"
namespace gm {
SceneView::~SceneView(){clear();}
void SceneView::clear(){hudView_.clear(renderer_);for(auto handle:images_)renderer_.releaseImage(handle);images_.clear();scene_=nullptr;}
void SceneView::render(SceneRuntime& scene,Rect viewport,bool showHud){
    if(scene_!=&scene){clear();std::vector<bgfx::TextureHandle> uploaded;try{for(const auto& image:scene.asset().imageBank.images)uploaded.push_back(renderer_.uploadImage(image));}catch(...){for(auto handle:uploaded)renderer_.releaseImage(handle);throw;}images_=std::move(uploaded);scene_=&scene;}
    viewport_=viewport;const auto& asset=scene.asset();float scale=std::min(viewport.w/asset.width,viewport.h/asset.height);float x=viewport.x+(viewport.w-asset.width*scale)*.5f,y=viewport.y+(viewport.h-asset.height*scale)*.5f;
    renderer_.clip(viewport);renderer_.rect({x,y,asset.width*scale,asset.height*scale},asset.background);
    for(const auto& sprite:scene.sprites()){Rect r{x+sprite.box.x*scale,y+sprite.box.y*scale,sprite.box.w*scale,sprite.box.h*scale};if(sprite.image==NoIndex)renderer_.rect(r,sprite.tint);else renderer_.image(r,images_.at(sprite.image),ImageFit::Contain,sprite.tint);}
    if(showHud)hudView_.render(renderer_,scene.hud(),viewport.w,viewport.h,viewport.x,viewport.y,false);renderer_.unclip();
}
uint32_t SceneView::hitUi(const SceneRuntime& scene,float x,float y)const{
    if(scene_!=&scene||!viewport_.contains(x,y))return NoIndex;x-=viewport_.x;y-=viewport_.y;
    for(auto it=scene.asset().bindings.rbegin();it!=scene.asset().bindings.rend();++it)if(it->kind==BindingKind::NumberChange&&it->node<hudView_.layout.clips().size()&&hudView_.layout.clips()[it->node].contains(x,y))return it->node;return NoIndex;
}
}
