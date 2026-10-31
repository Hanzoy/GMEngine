$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_scene,0);
uniform vec4 u_blur;
void main(){vec3 color=texture2D(s_scene,v_texcoord0).xyz*0.227027027;
color+=(texture2D(s_scene,v_texcoord0+u_blur.xy*1.3846153846).xyz+texture2D(s_scene,v_texcoord0-u_blur.xy*1.3846153846).xyz)*0.3162162162;
color+=(texture2D(s_scene,v_texcoord0+u_blur.xy*3.2307692308).xyz+texture2D(s_scene,v_texcoord0-u_blur.xy*3.2307692308).xyz)*0.0702702703;
gl_FragColor=vec4(color,1.0);}
