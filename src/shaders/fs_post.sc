$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_scene,0);
SAMPLER2D(s_bloom,1);
uniform vec4 u_post;
uniform vec4 u_postSize;
uniform vec4 u_bloom;
vec3 tonemap(vec3 value){vec3 x=max(value,vec3_splat(0.0))*u_post.x;if(u_post.z>0.5)x=clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);return pow(max(x,vec3_splat(0.0)),vec3_splat(1.0/u_post.y));}
vec3 sampleColor(vec2 uv){return tonemap(texture2D(s_scene,uv).xyz+texture2D(s_bloom,uv).xyz*u_bloom.y);}
float luma(vec3 color){return dot(color,vec3(0.299,0.587,0.114));}
void main(){vec2 uv=v_texcoord0;vec3 center=sampleColor(uv);
 if(u_post.w>0.5){vec2 step=u_postSize.xy;vec3 nw=sampleColor(uv+vec2(-step.x,-step.y)),ne=sampleColor(uv+vec2(step.x,-step.y)),sw=sampleColor(uv+vec2(-step.x,step.y)),se=sampleColor(uv+vec2(step.x,step.y));
 float lnw=luma(nw),lne=luma(ne),lsw=luma(sw),lse=luma(se),lc=luma(center);float lo=min(lc,min(min(lnw,lne),min(lsw,lse))),hi=max(lc,max(max(lnw,lne),max(lsw,lse)));
 if(hi-lo>max(0.0312,hi*0.125)){vec2 direction=vec2(-((lnw+lne)-(lsw+lse)),(lnw+lsw)-(lne+lse));float reduce=max((lnw+lne+lsw+lse)*0.03125,0.0078125);float inverse=1.0/(min(abs(direction.x),abs(direction.y))+reduce);direction=clamp(direction*inverse,-8.0,8.0)*step;
 vec3 a=(sampleColor(uv+direction*(1.0/3.0-0.5))+sampleColor(uv+direction*(2.0/3.0-0.5)))*0.5;
 vec3 b=a*0.5+(sampleColor(uv-direction*0.5)+sampleColor(uv+direction*0.5))*0.25;float lb=luma(b);center=(lb<lo||lb>hi)?a:b;}}
 gl_FragColor=vec4(center,1.0);}
