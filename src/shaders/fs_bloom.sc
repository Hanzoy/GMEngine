$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_scene,0);
uniform vec4 u_bloom;
void main(){vec3 color=texture2D(s_scene,v_texcoord0).xyz;float peak=max(max(color.x,color.y),color.z);float weight=max(peak-u_bloom.x,0.0)/max(peak,0.0001);gl_FragColor=vec4(color*weight,1.0);}
