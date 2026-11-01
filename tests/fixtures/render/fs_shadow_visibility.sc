$input v_world, v_normal, v_texcoord0
#include <bgfx_shader.sh>
#include "../../../src/shaders/gm_lighting.sh"
void main(){gl_FragColor=vec4(vec3_splat(gmShadow(v_world,normalize(v_normal))),1.0);}
