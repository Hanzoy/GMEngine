// Engine lighting ABI. Stages 0..7 belong to materials, 8..13 to the renderer.
// Include after bgfx_shader.sh; all vectors are world-space, radiance is linear HDR.
SAMPLER2D(s_gmShadow,8);
SAMPLER2D(s_gmIrradiance,9);
SAMPLER2D(s_gmRadiance,10);
SAMPLER2D(s_gmRsmPosition,11);
SAMPLER2D(s_gmRsmNormal,12);
SAMPLER2D(s_gmRsmFlux,13);
uniform vec4 u_gmLightRow0;
uniform vec4 u_gmLightRow1;
uniform vec4 u_gmLightRow2;
uniform vec4 u_gmClipUv;
uniform vec4 u_gmShadow;
uniform vec4 u_gmShadowExtra;
uniform vec4 u_gmEnvironment;
uniform vec4 u_gmRsm;
uniform vec4 u_gmRsmOrigin;
uniform vec4 u_gmRsmLimits;
vec3 gmLightCoord(vec3 world) {
    vec4 p=vec4(world,1.0);
    vec3 clip=vec3(dot(u_gmLightRow0,p),dot(u_gmLightRow1,p),dot(u_gmLightRow2,p));
    return vec3(clip.x*.5+.5,clip.y*u_gmClipUv.y+.5,clip.z*u_gmClipUv.z+u_gmClipUv.w);
}
float gmShadow(vec3 world,vec3 normal) {
    if(u_gmShadow.x<.5)return 1.0;
    vec3 p=gmLightCoord(world+normal*u_gmShadow.w);
    if(p.z<=0.0||p.z>=1.0||min(p.x,p.y)<=0.0||max(p.x,p.y)>=1.0)return 1.0;
    vec3 lightNormal=vec3(dot(normal,normalize(u_gmLightRow0.xyz)),dot(normal,normalize(u_gmLightRow1.xyz)),dot(normal,normalize(u_gmLightRow2.xyz)));
    float nz=(lightNormal.z<0.0?-1.0:1.0)*max(abs(lightNormal.z),.05);
    vec2 gradient=-lightNormal.xy/nz*u_gmShadowExtra.y*vec2(1.0,sign(u_gmClipUv.y));
    float bias=u_gmShadow.z+.5*u_gmShadow.y*(abs(gradient.x)+abs(gradient.y));
    float sum=0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        vec2 offset=vec2(float(x),float(y))*u_gmShadow.y*u_gmShadowExtra.x;
        float depth=texture2D(s_gmShadow,p.xy+offset).x;
        sum+=step(p.z+dot(gradient,offset)-bias,depth);
    }
    float edge=min(min(p.x,p.y),min(1.0-p.x,1.0-p.y));
    return mix(1.0,sum/9.0,smoothstep(0.0,.035,edge));
}
vec2 gmEnvironmentUv(vec3 direction) {
    vec3 d=vec3(direction.x*u_gmEnvironment.y-direction.z*u_gmEnvironment.z,direction.y,
                direction.x*u_gmEnvironment.z+direction.z*u_gmEnvironment.y);
    return vec2(atan2(d.z,d.x)*.1591549431+.5,acos(clamp(d.y,-1.0,1.0))*.3183098862);
}
vec3 gmEnvironment(vec3 n,vec3 v,vec3 base,float metal,float rough,float ao) {
    if(u_gmEnvironment.x<=0.0)return vec3_splat(0.0);
    float nv=max(dot(n,v),.0001);
    vec3 f0=mix(vec3_splat(.04),base,metal);
    vec3 fresnel=f0+(max(vec3_splat(1.0-rough),f0)-f0)*pow(1.0-nv,5.0);
    vec3 diffuse=texture2D(s_gmIrradiance,gmEnvironmentUv(n)).xyz*base*(1.0-metal)*(1.0-fresnel);
    vec3 reflection=reflect(-v,n);
    vec3 radiance=texture2DLod(s_gmRadiance,gmEnvironmentUv(reflection),rough*u_gmEnvironment.w).xyz;
    // Analytic split-sum DFG approximation (Brian Karis, Epic: Physically Based Shading on Mobile, 2014).
    vec4 r=rough*vec4(-1.0,-.0275,-.572,.022)+vec4(1.0,.0425,1.04,-.04);
    float a004=min(r.x*r.x,exp2(-9.28*nv))*r.x+r.y;
    vec2 ab=vec2(-1.04,1.04)*a004+r.zw;
    vec3 specular=radiance*max(f0*ab.x+ab.y,vec3_splat(0.0));
    return (diffuse+specular)*u_gmEnvironment.x*ao;
}
vec3 gmIndirect(vec3 world,vec3 normal,vec3 base,float metal,float ao) {
    if(u_gmRsm.x<.5||metal>=1.0)return vec3_splat(0.0);
    vec2 center=gmLightCoord(world).xy;
    if(min(center.x,center.y)<0.0||max(center.x,center.y)>1.0)return vec3_splat(0.0);
    vec3 result=vec3_splat(0.0);
    float rotation=fract(sin(dot(floor(world*32.0),vec3(12.9898,37.719,78.233)))*43758.5453)*6.2831853;
    for(int i=0;i<64;++i){
        if(float(i)>=u_gmRsm.z)break;
        float radius=sqrt((float(i)+.5)/u_gmRsm.z),angle=float(i)*2.39996323+rotation;
        vec2 uv=center+vec2(cos(angle),sin(angle))*radius*u_gmRsm.y;
        if(min(uv.x,uv.y)<=0.0||max(uv.x,uv.y)>=1.0)continue;
        vec4 position=texture2DLod(s_gmRsmPosition,uv,0.0);
        if(position.w<.5)continue;
        vec3 delta=position.xyz+u_gmRsmOrigin.xyz-world;
        float d2=dot(delta,delta);
        if(d2<u_gmRsmLimits.x||d2>u_gmRsmLimits.y)continue;
        vec3 direction=delta*inversesqrt(d2);
        vec3 emitter=texture2DLod(s_gmRsmNormal,uv,0.0).xyz*2.0-1.0;
        float geometry=max(dot(normal,direction),0.0)*max(dot(emitter,-direction),0.0)/d2;
        result+=texture2DLod(s_gmRsmFlux,uv,0.0).xyz*geometry;
    }
    return result*u_gmRsm.w*base*(1.0-metal)*.3183098862*ao;
}
