// Shared metallic/roughness BRDF. specular.rgb is dielectric F0 before weighting;
// specular.w scales the dielectric lobe (including grazing angles), never metals.
// Default vec4(.04,.04,.04,1) preserves existing materials.
vec3 gmF0(vec3 base,float metal,vec4 specular){
    return mix(clamp(specular.xyz,0.0,1.0)*clamp(specular.w,0.0,1.0),base,metal);
}
float gmF90(float metal,vec4 specular){return mix(clamp(specular.w,0.0,1.0),1.0,metal);}
float gmMax3(vec3 v){return max(v.x,max(v.y,v.z));}
float gmDiffuseWeight(float cosine,float metal,vec4 specular){
    vec3 f=clamp(specular.xyz,0.0,1.0);
    f=(f+(1.0-f)*pow(1.0-cosine,5.0))*clamp(specular.w,0.0,1.0);
    return (1.0-gmMax3(f))*(1.0-metal);
}
vec3 gmDirectBrdf(vec3 n,vec3 v,vec3 l,vec3 base,float metal,float rough,vec4 specular){
    vec3 halfway=v+l;
    vec3 h=halfway*inversesqrt(max(dot(halfway,halfway),.00000001));
    float nl=max(dot(n,l),0.0),nv=max(dot(n,v),.0001),nh=max(dot(n,h),0.0),vh=max(dot(v,h),0.0);
    float a=rough*rough,a2=a*a,den=nh*nh*(a2-1.0)+1.0;
    float distribution=a2/max(3.14159265*den*den,.00001),k=(rough+1.0)*(rough+1.0)/8.0;
    float geometry=nl/(nl*(1.0-k)+k)*nv/(nv*(1.0-k)+k);
    vec3 f0=gmF0(base,metal,specular);
    vec3 f=f0+(gmF90(metal,specular)-f0)*pow(1.0-vh,5.0);
    return gmDiffuseWeight(vh,metal,specular)*base*.3183098862+distribution*geometry*f/max(4.0*nl*nv,.0001);
}
// Reconstruct an orthonormal tangent frame against the interpolated normal.
// Independently normalized position derivatives instead follow each flat triangle,
// producing a distorted normal lobe on smooth surfaces / skewed UVs.
vec3 gmMappedNormal(vec3 world,vec2 uv,vec3 normal,vec3 map){
    vec3 n=normalize(normal),q1=dFdx(world),q2=dFdy(world);
    vec2 s1=dFdx(uv),s2=dFdy(uv);
    float determinant=s1.x*s2.y-s1.y*s2.x;
    vec3 t=(q1*s2.y-q2*s1.y)*sign(determinant);
    vec3 sourceB=(-q1*s2.x+q2*s1.x)*sign(determinant);
    t-=n*dot(n,t);
    if(dot(t,t)<.00000001||dot(sourceB,sourceB)<.00000001||abs(determinant)<.00000001)return n;
    t=normalize(t);
    float orientation=dot(cross(n,t),sourceB);
    if(abs(orientation)<.00000001)return n;
    vec3 b=cross(n,t)*sign(orientation);
    return normalize(t*map.x+b*map.y+n*map.z);
}
