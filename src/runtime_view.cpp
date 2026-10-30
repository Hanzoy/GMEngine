#include "runtime_view.hpp"
#include <cmath>
namespace gm {
void RuntimeView::clear(Renderer& renderer) {if(renderer_==&renderer)for(auto handle:images_)renderer.releaseImage(handle);images_.clear();renderer_=nullptr;document_=nullptr;assetRevision_=0;layout=RuntimeLayout{};}
void RuntimeView::render(Renderer& renderer,RuntimeDocument& document,float width,float height,float offsetX,float offsetY,bool rootBackground) {
    if(renderer_!=&renderer || document_!=&document || assetRevision_!=document.assetRevision()) {
        document.restoreImagePixels();
        std::vector<bgfx::TextureHandle> next;
        next.reserve(document.images().size());
        try {for(const auto& image:document.images()) next.push_back(renderer.uploadImage(image));}
        catch(...) {for(auto handle:next) renderer.releaseImage(handle);throw;}
        if(renderer_==&renderer) for(auto handle:images_) renderer.releaseImage(handle);
        images_=std::move(next);renderer_=&renderer;document_=&document;assetRevision_=document.assetRevision();
        document.releaseImagePixels();
    }
    layout.update(document,width,height);const float scale=layout.scale();
    for(auto i:layout.visibleNodes()) {
        const auto& node=document.nodes()[i];auto r=layout.boxes()[i];r.x+=offsetX;r.y+=offsetY;auto clip=layout.clips()[i];clip.x+=offsetX;clip.y+=offsetY;
        if(node.kind==UiKind::Image) {renderer.clip(clip);renderer.image(r,images_[node.image],node.fit,node.tint);continue;}
        // Solid backgrounds already have exact clipped geometry. Reusing the full
        // viewport scissor lets adjacent solids batch without changing paint order.
        if(node.kind!=UiKind::Label && (i!=0||rootBackground)) {renderer.unclip();renderer.rect(clip,node.color);}
        if(node.kind==UiKind::List) {
            const auto &rows=document.list(i);const float offset=std::min(document.scrollOffset(i),std::max(0.f,float(rows.size())*node.rowHeight-r.h/scale+2*node.padding));
            const size_t first=size_t(offset/node.rowHeight),last=std::min(rows.size(),first+size_t(std::ceil(r.h/(node.rowHeight*scale)))+1);
            renderer.clip(intersect(clip,{r.x+node.padding*scale,r.y+node.padding*scale,std::max(0.f,r.w-2*node.padding*scale),std::max(0.f,r.h-2*node.padding*scale)}));
            for(size_t row=first;row<last;++row)renderer.text(r.x+node.padding*scale,r.y+(node.padding+float(row)*node.rowHeight-offset)*scale,rows[row],node.fontSize*scale,node.textColor,std::max(0.f,r.w-2*node.padding*scale));
            continue;
        }
        if(node.kind!=UiKind::Panel && !node.text.empty()) {
            renderer.clip(clip);const float font=node.fontSize*scale;
            const float pad=node.kind==UiKind::Button?std::max(6*scale,(r.w-renderer.textWidth(node.text,font))*.5f):0;
            renderer.text(r.x+pad,r.y+(node.kind==UiKind::Button?std::max(0.f,(r.h-font)*.5f-2*scale):0),node.text,font,node.textColor,r.w-pad-3*scale);
        }
    }
    renderer.unclip();
}
}
