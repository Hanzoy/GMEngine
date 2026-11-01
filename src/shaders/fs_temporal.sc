$input v_texcoord0
#include <bgfx_shader.sh>
#include "gm_screen.sh"
SAMPLER2D(s_scene,0);
SAMPLER2D(s_history,3);
uniform vec4 u_reproject0;
uniform vec4 u_reproject1;
uniform vec4 u_reproject2;
uniform vec4 u_reproject3;
uniform vec4 u_temporal; // valid, history weight
vec3 compressColor(vec3 c){return c/(1.0+dot(c,vec3(.2126,.7152,.0722)));}
vec3 expandColor(vec3 c){return c/max(1.0-dot(c,vec3(.2126,.7152,.0722)),.0001);}
vec3 reconstruct(sampler2D source,vec2 uv){
    vec2 position=uv/u_screenSize.xy-.5,base=floor(position),f=position-base;
    vec2 w0=f*(-.5+f*(1.0-.5*f));
    vec2 w1=1.0+f*f*(-2.5+1.5*f);
    vec2 w2=f*(.5+f*(2.0-1.5*f));
    vec2 w3=f*f*(-.5+.5*f),w12=w1+w2;
    vec2 p0=(base-.5)*u_screenSize.xy,p12=(base+.5+w2/w12)*u_screenSize.xy,p3=(base+2.5)*u_screenSize.xy;
    vec3 c=texture2D(source,vec2(p0.x,p0.y)).xyz*w0.x*w0.y
        +texture2D(source,vec2(p12.x,p0.y)).xyz*w12.x*w0.y
        +texture2D(source,vec2(p3.x,p0.y)).xyz*w3.x*w0.y
        +texture2D(source,vec2(p0.x,p12.y)).xyz*w0.x*w12.y
        +texture2D(source,vec2(p12.x,p12.y)).xyz*w12.x*w12.y
        +texture2D(source,vec2(p3.x,p12.y)).xyz*w3.x*w12.y
        +texture2D(source,vec2(p0.x,p3.y)).xyz*w0.x*w3.y
        +texture2D(source,vec2(p12.x,p3.y)).xyz*w12.x*w3.y
        +texture2D(source,vec2(p3.x,p3.y)).xyz*w3.x*w3.y;
    return max(c,vec3_splat(0.0));
}
void main(){
    // Resolve onto a stable output grid. History is unjittered; only the current source is jittered.
    vec2 outputUv=v_texcoord0;
    vec2 uv=outputUv+u_screenJitter.xy*vec2(.5,.5*u_screenSize.z);
    float depth=texture2D(s_depth,uv).x;
    vec3 current=reconstruct(s_scene,uv);
    float z=depth>=.999999?0.0:gmViewDepth(depth);
    vec4 clip=vec4((uv*2.0-1.0)*vec2(1.0,u_screenSize.z),mix(depth,depth*2.0-1.0,u_screenSize.w),1.0);
    vec4 previous=vec4(dot(u_reproject0,clip),dot(u_reproject1,clip),dot(u_reproject2,clip),dot(u_reproject3,clip));
    vec2 previousUv=previous.xy/max(previous.w,1e-6)*vec2(.5,.5*u_screenSize.z)+.5;
    vec4 history=texture2D(s_history,previousUv);
    // previous.w/current homogeneous w gives previous camera-space linear depth.
    float expectedZ=previous.w*z;
    bool valid=u_temporal.x>.5&&z>0.0&&previous.w>0.0&&min(previousUv.x,previousUv.y)>0.0&&max(previousUv.x,previousUv.y)<1.0;
    valid=valid&&abs(history.w-expectedZ)<max(.025,expectedZ*.015);
    if(valid){
        vec3 lo=vec3_splat(1e10),hi=vec3_splat(-1e10),mean=vec3_splat(0.0),second=vec3_splat(0.0);
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
            vec3 c=compressColor(texture2D(s_scene,uv+vec2(float(x),float(y))*u_screenSize.xy).xyz);
            lo=min(lo,c);hi=max(hi,c);mean+=c;second+=c*c;
        }
        mean/=9.0;vec3 sigma=sqrt(max(second/9.0-mean*mean,vec3_splat(0.0)));
        lo=max(lo,mean-1.5*sigma);hi=min(hi,mean+1.5*sigma);
        vec3 c=compressColor(current),h=clamp(compressColor(reconstruct(s_history,previousUv)),lo,hi);
        float motion=length((previousUv-outputUv)/u_screenSize.xy);
        float weight=u_temporal.y*clamp(1.0-motion*.025,.5,1.0);
        current=expandColor(mix(c,h,weight));
    }
    gl_FragColor=vec4(current,min(z,65504.0));
}
