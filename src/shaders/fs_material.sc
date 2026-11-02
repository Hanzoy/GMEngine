$input v_world, v_normal, v_texcoord0
#include <bgfx_shader.sh>
#include "gm_lighting.sh"
SAMPLER2D(s_baseColor,0);
SAMPLER2D(s_normal,1);
uniform vec4 u_baseColor;
uniform vec4 u_surface;
uniform vec4 u_specular;
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
        vec3 map=texture2D(s_normal,uv).xyz*2.0-1.0;map.xy*=u_surface.w;
        n=gmMappedNormal(v_world,uv,n,map);
    }
    vec3 v=normalize(u_camera.xyz-v_world),l=normalize(-u_lightDirection.xyz);
    float nl=max(dot(n,l),0.0);
    float rough=clamp(u_surface.y,0.045,1.0),metal=clamp(u_surface.x,0.0,1.0);
    rough=gmFilteredRoughness(n,rough);
    float ao=gmAmbientOcclusion(v_world);
    vec3 color=gmDirectBrdf(n,v,l,base.xyz,metal,rough,u_specular)*u_lightColor.xyz*nl*gmShadow(v_world,normalize(v_normal))+base.xyz*u_lightColor.w*ao+emission;
    color+=gmEnvironment(n,v,base.xyz,metal,rough,ao,u_specular)+gmIndirect(v_world,n,base.xyz,metal,ao);
    color+=gmLocalLighting(v_world,n,v,base.xyz,metal,rough,u_specular);
    color=gmPlanarReflection(color,v_world,n,v,rough,u_planar.x);
    gl_FragColor=vec4(gmHeightFog(color,v_world,u_camera.xyz),base.a);
}
