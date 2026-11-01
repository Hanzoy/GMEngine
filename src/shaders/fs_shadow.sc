$input v_world, v_normal, v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_baseColor,0);
uniform vec4 u_baseColor;
uniform vec4 u_surface;
uniform vec4 u_uvTransform;
void main(){
    float alpha=texture2D(s_baseColor,v_texcoord0*u_uvTransform.xy+u_uvTransform.zw).a*u_baseColor.a;
    if(alpha<u_surface.z)discard;
    gl_FragColor=vec4_splat(1.0);
}
