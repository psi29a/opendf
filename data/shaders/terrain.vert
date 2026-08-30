#version 120
#extension GL_EXT_gpu_shader4 : require
#extension GL_ARB_draw_instanced : enable

uniform usampler2D tilemapTex;

varying vec3 pos_viewspace;
varying vec3 n_viewspace;
varying vec3 t_viewspace;
varying vec3 b_viewspace;
varying vec4 TexCoords;
flat varying uint TexIndex;

void main()
{
    int x = gl_InstanceIDARB & 15;
    int y = gl_InstanceIDARB / 16;
    vec4 pos = gl_Vertex + vec4(x*256, 0, y*-256, 0);

    gl_Position = gl_ModelViewProjectionMatrix * pos;
    TexCoords = gl_MultiTexCoord0;
    TexIndex = texelFetch2D(tilemapTex, ivec2(x, y), 0).r;

    pos_viewspace = (gl_ModelViewMatrix * pos).xyz;

    vec3 normal   = vec3(0.0, -1.0, 0.0);
    vec3 binormal = vec3(1.0,  0.0, 0.0);
    n_viewspace = normalize(gl_NormalMatrix * normal);
    t_viewspace = normalize(mat3(gl_ModelViewMatrix) * cross(normal, binormal));
    b_viewspace = normalize(mat3(gl_ModelViewMatrix) * binormal);
}
