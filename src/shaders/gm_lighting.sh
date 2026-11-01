// Engine lighting ABI. Stages 0..7 belong to materials, 8..14 to the renderer.
// Include after bgfx_shader.sh; all vectors are world-space, radiance is linear HDR.
SAMPLER2D(s_gmShadow,8);
SAMPLER2D(s_gmIrradiance,9);
SAMPLER2D(s_gmRadiance,10);
SAMPLER2D(s_gmRsmPosition,11);
SAMPLER2D(s_gmRsmNormal,12);
SAMPLER2D(s_gmRsmFlux,13);
SAMPLER2D(s_gmAmbient,14);
uniform vec4 u_gmScreen0;
uniform vec4 u_gmScreen1;
uniform vec4 u_gmScreen3;
uniform vec4 u_gmAmbient;
float gmAmbientOcclusion(vec3 world){
    if(u_gmAmbient.x<.5)return 1.0;
    vec4 p=vec4(world,1.0);
    vec2 xy=vec2(dot(u_gmScreen0,p),dot(u_gmScreen1,p))/max(dot(u_gmScreen3,p),1e-5);
    return texture2D(s_gmAmbient,xy*vec2(.5,u_gmAmbient.y)+.5).x;
}
// Filter the specular lobe over the pixel footprint instead of letting tiny highlights sparkle.
float gmFilteredRoughness(vec3 normal,float roughness){
    vec3 dx=dFdx(normal),dy=dFdy(normal);
    float variance=.25*(dot(dx,dx)+dot(dy,dy));
    float r2=roughness*roughness;
    return sqrt(sqrt(clamp(r2*r2+min(2.0*variance,.18),0.000004,1.0)));
}
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
// Interpolate comparison results, never raw depths across unrelated occluders.
float gmShadowCompare(vec3 receiver,vec2 uv,vec2 gradient,float bias){
    return step(receiver.z+dot(gradient,uv-receiver.xy)-bias,texture2D(s_gmShadow,uv).x);
}
float gmShadowBilinear(vec3 receiver,vec2 uv,vec2 gradient,float bias){
    vec2 pixel=uv/u_gmShadow.y-.5,f=fract(pixel),base=(floor(pixel)+.5)*u_gmShadow.y;
    float a=gmShadowCompare(receiver,base,gradient,bias);
    float b=gmShadowCompare(receiver,base+vec2(u_gmShadow.y,0.0),gradient,bias);
    float c=gmShadowCompare(receiver,base+vec2(0.0,u_gmShadow.y),gradient,bias);
    float d=gmShadowCompare(receiver,base+vec2_splat(u_gmShadow.y),gradient,bias);
    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
}
float gmShadow(vec3 world,vec3 normal) {
    if(u_gmShadow.x<.5)return 1.0;
    vec3 p=gmLightCoord(world+normal*u_gmShadow.w);
    // The rasterized receiver plane differs from interpolated smooth vertex normals.
    // Solve its depth gradient in shadow UV space to avoid triangle-shaped self-shadow acne.
    vec3 receiver=gmLightCoord(world),dx=dFdx(receiver),dy=dFdy(receiver);
    float determinant=dx.x*dy.y-dx.y*dy.x;
    vec2 gradient=abs(determinant)>1e-12?vec2(dx.z*dy.y-dy.z*dx.y,dx.x*dy.z-dy.x*dx.z)/determinant:vec2_splat(0.0);
    gradient=clamp(gradient,vec2_splat(-50.0),vec2_splat(50.0));
    float bias=u_gmShadow.z+.5*u_gmShadow.y*(abs(gradient.x)+abs(gradient.y));
    if(p.z<=0.0||p.z>=1.0||min(p.x,p.y)<=0.0||max(p.x,p.y)>=1.0)return 1.0;
    float visibility=0.0;
    if(abs(u_gmShadowExtra.x-1.0)<.0001){
        // Nine overlapping bilinear comparisons combine into 16 weighted point comparisons.
        vec2 pixel=p.xy/u_gmShadow.y-.5,f=fract(pixel),base=(floor(pixel)+.5)*u_gmShadow.y;
        for(int y=-1;y<=2;++y)for(int x=-1;x<=2;++x){
            float wx=x==-1?1.0-f.x:(x==2?f.x:1.0);
            float wy=y==-1?1.0-f.y:(y==2?f.y:1.0);
            visibility+=wx*wy*gmShadowCompare(p,base+vec2(float(x),float(y))*u_gmShadow.y,gradient,bias);
        }
        visibility/=9.0;
    }else if(u_gmShadowExtra.x<.0001){
        visibility=gmShadowBilinear(p,p.xy,gradient,bias);
    }else{
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
            vec2 uv=p.xy+vec2(float(x),float(y))*u_gmShadow.y*u_gmShadowExtra.x;
            visibility+=gmShadowBilinear(p,uv,gradient,bias);
        }
        visibility/=9.0;
    }
    float edge=min(min(p.x,p.y),min(1.0-p.x,1.0-p.y));
    return mix(1.0,visibility,smoothstep(0.0,.035,edge));
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
    // Fixed low-discrepancy pattern avoids discontinuous world-voxel random rotations.
    float rotation=.37;
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
