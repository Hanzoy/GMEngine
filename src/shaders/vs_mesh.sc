$input a_position, a_normal, a_texcoord0
$output v_world, v_normal, v_texcoord0
#include <bgfx_shader.sh>
void main() {
    mat4 model=u_model[0];
    vec4 world=mul(model,vec4(a_position,1.0));
    vec3 c0=mul(model,vec4(1.0,0.0,0.0,0.0)).xyz,c1=mul(model,vec4(0.0,1.0,0.0,0.0)).xyz,c2=mul(model,vec4(0.0,0.0,1.0,0.0)).xyz;
    v_normal=normalize(cross(c1,c2)*a_normal.x+cross(c2,c0)*a_normal.y+cross(c0,c1)*a_normal.z);
    v_world=world.xyz;v_texcoord0=a_texcoord0;
    gl_Position=mul(u_viewProj,world);
}
