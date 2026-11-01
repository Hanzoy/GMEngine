// Sampled depth reconstruction; texture UV follows the active bgfx render target origin.
SAMPLER2D(s_depth,2);
uniform vec4 u_screenProjection; // 1/Pxx, 1/Pyy, A, B
uniform vec4 u_screenSize; // 1/w, 1/h, NDC y sign, homogeneous depth
uniform vec4 u_screenJitter;
float gmViewDepth(float depth) {
    float ndc=mix(depth,depth*2.0-1.0,u_screenSize.w);
    return u_screenProjection.w/(ndc-u_screenProjection.z);
}
vec3 gmViewPosition(vec2 uv,float depth) {
    float z=gmViewDepth(depth);
    vec2 xy=(uv*2.0-1.0)*vec2(1.0,u_screenSize.z)-u_screenJitter.xy;
    return vec3(xy*u_screenProjection.xy*z,z);
}
