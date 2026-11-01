$input v_texcoord0
#include <bgfx_shader.sh>
#include "gm_screen.sh"
uniform vec4 u_ao; // world radius, intensity, angle bias
void main() {
    // AO runs at half resolution. Point-sampled depth belongs to a full-resolution
    // texel center, not to the arbitrary UV used to request it. Mixing those two
    // positions bends reconstructed flat surfaces into screen-aligned dark bands.
    vec2 uv=(floor(v_texcoord0/u_screenSize.xy)+.5)*u_screenSize.xy;
    float depth=texture2D(s_depth,uv).x;
    if(depth>=.999999){gl_FragColor=vec4_splat(1.0);return;}
    vec3 p=gmViewPosition(uv,depth);
    vec2 dx=vec2(u_screenSize.x,0.0),dy=vec2(0.0,u_screenSize.y);
    vec3 left=gmViewPosition(uv-dx,texture2D(s_depth,uv-dx).x)-p;
    vec3 right=gmViewPosition(uv+dx,texture2D(s_depth,uv+dx).x)-p;
    vec3 up=gmViewPosition(uv-dy,texture2D(s_depth,uv-dy).x)-p;
    vec3 down=gmViewPosition(uv+dy,texture2D(s_depth,uv+dy).x)-p;
    vec3 tangent=abs(left.z)<abs(right.z)?-left:right;
    vec3 bitangent=abs(up.z)<abs(down.z)?-up:down;
    vec3 n=normalize(cross(tangent,bitangent));
    if(dot(n,p)>0.0)n=-n;
    vec2 radius=u_ao.x/(2.0*p.z*u_screenProjection.xy);
    radius=min(radius,vec2_splat(.25));
    float obscurance=0.0;
    // Fixed angular pattern: no per-frame or per-pixel noise to sparkle in motion.
    for(int i=0;i<8;++i){
        float angle=(float(i)+.5)*.7853981634;
        vec2 ray=vec2(cos(angle),sin(angle));
        float horizon=0.0;
        for(int j=1;j<=4;++j){
            float t=float(j)/4.0;
            vec2 sampleUv=uv+ray*radius*t*t;
            if(min(sampleUv.x,sampleUv.y)<=0.0||max(sampleUv.x,sampleUv.y)>=1.0)continue;
            sampleUv=(floor(sampleUv/u_screenSize.xy)+.5)*u_screenSize.xy;
            float sd=texture2D(s_depth,sampleUv).x;
            if(sd>=.999999)continue;
            vec3 delta=gmViewPosition(sampleUv,sd)-p;
            float d2=dot(delta,delta);
            float contribution=max(dot(n,delta)*inversesqrt(max(d2,1e-8))-u_ao.z,0.0);
            contribution*=max(1.0-d2/(u_ao.x*u_ao.x),0.0);
            horizon=max(horizon,contribution);
        }
        obscurance+=horizon;
    }
    float visibility=clamp(1.0-u_ao.y*obscurance/8.0,0.05,1.0);
    gl_FragColor=vec4_splat(visibility);
}
