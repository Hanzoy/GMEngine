$input a_position, a_normal, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_world, v_normal, v_texcoord0
#include <bgfx_shader.sh>
void main() {
    mat4 model=mtxFromCols(i_data0,i_data1,i_data2,i_data3);
    vec4 world=mul(model,vec4(a_position,1.0));
    vec3 c0=i_data0.xyz,c1=i_data1.xyz,c2=i_data2.xyz;
    v_normal=normalize(cross(c1,c2)*a_normal.x+cross(c2,c0)*a_normal.y+cross(c0,c1)*a_normal.z);
    v_world=world.xyz;v_texcoord0=a_texcoord0;
    gl_Position=mul(u_viewProj,world);
}
