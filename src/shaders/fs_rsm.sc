$input v_world, v_normal, v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_baseColor,0);
SAMPLER2D(s_orm,2);
uniform vec4 u_baseColor;
uniform vec4 u_surface;
uniform vec4 u_uvTransform;
uniform vec4 u_gmRsmOrigin;
uniform vec4 u_gmRsmMaterial;
uniform vec4 u_lightColor;
uniform vec4 u_lightDirection;
void main(){
    vec2 uv=v_texcoord0*u_uvTransform.xy+u_uvTransform.zw;
    vec4 base=texture2D(s_baseColor,uv)*u_baseColor;
    if(base.a<u_surface.z)discard;
    float metal=u_surface.x*mix(1.0,texture2D(s_orm,uv).b,u_gmRsmMaterial.x);
    gl_FragData[0]=vec4(v_world-u_gmRsmOrigin.xyz,1.0);
    gl_FragData[1]=vec4(normalize(v_normal)*.5+.5,1.0);
    // Reflected Lambertian radiance times surface area / projected texel area.
    // The incident cosine cancels that area conversion. Receiver geometry is gathered later.
    gl_FragData[2]=vec4(base.xyz*(1.0-metal)*u_lightColor.xyz*.3183098862*u_gmRsmMaterial.y*step(.0001,dot(normalize(v_normal),normalize(-u_lightDirection.xyz))),1.0);
}
