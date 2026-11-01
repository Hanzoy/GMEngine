$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_gmRadiance,10);
uniform vec4 u_gmEnvironment;
uniform vec4 u_gmSkyRight;
uniform vec4 u_gmSkyUp;
uniform vec4 u_gmSkyForward;
void main(){
    vec2 p=v_texcoord0*2.0-1.0;
    vec3 d=normalize(u_gmSkyForward.xyz+u_gmSkyRight.xyz*p.x-u_gmSkyUp.xyz*p.y);
    d=vec3(d.x*u_gmEnvironment.y-d.z*u_gmEnvironment.z,d.y,d.x*u_gmEnvironment.z+d.z*u_gmEnvironment.y);
    vec2 uv=vec2(atan2(d.z,d.x)*.1591549431+.5,acos(clamp(d.y,-1.0,1.0))*.3183098862);
    gl_FragColor=vec4(texture2DLod(s_gmRadiance,uv,0.0).xyz*u_gmEnvironment.x,1.0);
}
