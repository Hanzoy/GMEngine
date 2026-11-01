$input v_world, v_normal, v_texcoord0
#include <bgfx_shader.sh>
#include "gm_lighting.sh"
SAMPLER2D(s_baseColor,0);
SAMPLER2D(s_normal,1);
uniform vec4 u_baseColor;
uniform vec4 u_surface;
uniform vec4 u_emission;
uniform vec4 u_unlit;
uniform vec4 u_planar;
uniform vec4 u_uvTransform;
uniform vec4 u_camera;
uniform vec4 u_lightDirection;
uniform vec4 u_lightColor;
void main() {
    gmClipReflection(v_world);
    vec2 uv=v_texcoord0*u_uvTransform.xy+u_uvTransform.zw;
    vec4 base=texture2D(s_baseColor,uv)*u_baseColor;
    if(base.a<u_surface.z)discard;
    vec3 emission=u_emission.xyz*u_emission.w;
    if(u_unlit.x>0.5){gl_FragColor=vec4(gmHeightFog(base.xyz+emission,v_world,u_camera.xyz),base.a);return;}
    vec3 n=normalize(v_normal);
    if(u_surface.w>0.0){
        vec3 q1=dFdx(v_world),q2=dFdy(v_world);vec2 s1=dFdx(uv),s2=dFdy(uv);
        vec3 t=q1*s2.y-q2*s1.y;vec3 b=-q1*s2.x+q2*s1.x;
        float determinant=s1.x*s2.y-s1.y*s2.x;
        if(dot(t,t)>0.00000001&&dot(b,b)>0.00000001&&abs(determinant)>0.00000001){
            t=normalize(t)*sign(determinant);b=normalize(b)*sign(determinant);
            vec3 map=texture2D(s_normal,uv).xyz*2.0-1.0;map.xy*=u_surface.w;
            n=normalize(t*map.x+b*map.y+n*map.z);
        }
    }
    vec3 v=normalize(u_camera.xyz-v_world),l=normalize(-u_lightDirection.xyz),h=normalize(v+l);
    float nl=max(dot(n,l),0.0),nv=max(dot(n,v),0.0001),nh=max(dot(n,h),0.0),vh=max(dot(v,h),0.0);
    float rough=clamp(u_surface.y,0.045,1.0),metal=clamp(u_surface.x,0.0,1.0);
    rough=gmFilteredRoughness(n,rough);
    float a=rough*rough,a2=a*a,den=nh*nh*(a2-1.0)+1.0;
    float distribution=a2/max(3.14159265*den*den,0.00001);
    float k=(rough+1.0)*(rough+1.0)/8.0;
    float geometry=nl/(nl*(1.0-k)+k)*nv/(nv*(1.0-k)+k);
    vec3 f0=mix(vec3_splat(0.04),base.xyz,metal);
    vec3 fresnel=f0+(1.0-f0)*pow(1.0-vh,5.0);
    vec3 specular=distribution*geometry*fresnel/max(4.0*nl*nv,0.0001);
    vec3 diffuse=(1.0-fresnel)*(1.0-metal)*base.xyz/3.14159265;
    float ao=gmAmbientOcclusion(v_world);
    vec3 color=(diffuse+specular)*u_lightColor.xyz*nl*gmShadow(v_world,normalize(v_normal))+base.xyz*u_lightColor.w*ao+emission;
    color+=gmEnvironment(n,v,base.xyz,metal,rough,ao)+gmIndirect(v_world,n,base.xyz,metal,ao);
    color+=gmLocalLighting(v_world,n,v,base.xyz,metal,rough);
    color=gmPlanarReflection(color,v_world,n,v,rough,u_planar.x);
    gl_FragColor=vec4(gmHeightFog(color,v_world,u_camera.xyz),base.a);
}
