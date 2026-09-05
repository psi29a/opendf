#version 120
#extension GL_EXT_gpu_shader4 : require
#extension GL_ARB_draw_instanced : enable

uniform usampler2D tilemapTex;

varying vec3 pos_viewspace;
varying vec3 n_viewspace;
varying vec3 t_viewspace;
varying vec3 b_viewspace;
varying vec4 TexCoords;
// A float rather than a uint: Apple's GLSL 1.20 compiler rejects a uint
// varying outright ("syntax error"), even though it advertises
// GL_EXT_gpu_shader4 and accepts usampler2D, texelFetch2D and
// gl_InstanceIDARB from the same extension. Still flat, so the index is not
// interpolated; a float carries tile indices exactly either way.
flat varying float TexIndex;

void main()
{
    int x = gl_InstanceIDARB & 15;
    int y = gl_InstanceIDARB / 16;
    vec4 pos = gl_Vertex + vec4(x*256, 0, y*-256, 0);

    gl_Position = gl_ModelViewProjectionMatrix * pos;
    TexCoords = gl_MultiTexCoord0;
    TexIndex = float(texelFetch2D(tilemapTex, ivec2(x, y), 0).r);

    pos_viewspace = (gl_ModelViewMatrix * pos).xyz;

    vec3 normal   = vec3(0.0, -1.0, 0.0);
    vec3 binormal = vec3(1.0,  0.0, 0.0);
    n_viewspace = normalize(gl_NormalMatrix * normal);
    t_viewspace = normalize(mat3(gl_ModelViewMatrix) * cross(normal, binormal));
    b_viewspace = normalize(mat3(gl_ModelViewMatrix) * binormal);
}
