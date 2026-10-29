#include "scene_view.hpp"
#include <cmath>
namespace gm {
SceneView::~SceneView(){clear();}
void SceneView::clear(){hudView_.clear(renderer_);for(auto handle:images_)renderer_.releaseImage(handle);images_.clear();scene_=nullptr;}
void SceneView::render(SceneRuntime& scene,Rect viewport,bool showHud){
    if(scene_!=&scene){clear();std::vector<bgfx::TextureHandle> uploaded;try{for(const auto& image:scene.asset().imageBank.images)uploaded.push_back(renderer_.uploadImage(image));}catch(...){for(auto handle:uploaded)renderer_.releaseImage(handle);throw;}images_=std::move(uploaded);scene_=&scene;}
    viewport_=viewport;const auto& asset=scene.asset();float scale=std::min(viewport.w/asset.width,viewport.h/asset.height);float x=viewport.x+(viewport.w-asset.width*scale)*.5f,y=viewport.y+(viewport.h-asset.height*scale)*.5f;
    renderer_.clip(intersect(viewport,{x,y,asset.width*scale,asset.height*scale}));renderer_.rect({x,y,asset.width*scale,asset.height*scale},asset.background);
    auto camera=scene.camera();const auto& c=camera.transform;float determinant=c.a*c.d-c.b*c.c;if(std::abs(determinant)<1e-9f)throw std::runtime_error("Camera transform is singular");
    for(const auto& sprite:scene.sprites()){std::array<float,8> points{};const auto& t=sprite.transform;for(int i=0;i<4;++i){float u=(i==1||i==2)?sprite.box.w:0,v=i>=2?sprite.box.h:0;float wx=t.x+t.a*u+t.c*v-c.x,wy=t.y+t.b*u+t.d*v-c.y;points[i*2]=x+(c.d*wx-c.c*wy)/determinant*camera.zoom*scale;points[i*2+1]=y+(-c.b*wx+c.a*wy)/determinant*camera.zoom*scale;}const auto& uv=sprite.region;renderer_.sprite(points,sprite.image==NoIndex?bgfx::TextureHandle BGFX_INVALID_HANDLE:images_.at(sprite.image),sprite.tint,{uv.u0,uv.v0,uv.u1-uv.u0,uv.v1-uv.v0});}
    if(showHud)hudView_.render(renderer_,scene.hud(),viewport.w,viewport.h,viewport.x,viewport.y,false);renderer_.unclip();
}
uint32_t SceneView::hitUi(const SceneRuntime& scene,float x,float y)const{
    if(scene_!=&scene||!viewport_.contains(x,y))return NoIndex;x-=viewport_.x;y-=viewport_.y;
    for(auto it=scene.asset().bindings.rbegin();it!=scene.asset().bindings.rend();++it)if(it->kind==BindingKind::NumberChange&&it->node<hudView_.layout.clips().size()&&hudView_.layout.clips()[it->node].contains(x,y))return it->node;return NoIndex;
}
}
