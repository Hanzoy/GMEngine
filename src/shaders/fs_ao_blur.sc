$input v_texcoord0
#include <bgfx_shader.sh>
#include "gm_screen.sh"
SAMPLER2D(s_scene,0);
uniform vec4 u_aoBlur;
void main(){
    float center=gmViewDepth(texture2D(s_depth,v_texcoord0).x);
    float result=0.0,weight=0.0;
    for(int i=-2;i<=2;++i){
        vec2 uv=v_texcoord0+u_aoBlur.xy*float(i);
        float z=gmViewDepth(texture2D(s_depth,uv).x);
        float w=exp2(-float(i*i)*.5-abs(z-center)/max(.015,center*.005));
        result+=texture2D(s_scene,uv).x*w;weight+=w;
    }
    gl_FragColor=vec4_splat(result/max(weight,1e-6));
}
