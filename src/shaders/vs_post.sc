$input a_position, a_texcoord0
$output v_texcoord0
#include <bgfx_shader.sh>
uniform vec4 u_postUv;
void main(){gl_Position=vec4(a_position,0.0,1.0);v_texcoord0=vec2(a_texcoord0.x,mix(a_texcoord0.y,1.0-a_texcoord0.y,u_postUv.x));}
