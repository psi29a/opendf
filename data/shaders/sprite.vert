#version 120

uniform float CurrentFrame;

varying vec3 pos_viewspace;
varying vec3 n_viewspace;
varying vec3 t_viewspace;
varying vec3 b_viewspace;
varying vec4 TexCoords;
varying vec4 Color;

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    TexCoords = gl_MultiTexCoord0;
    TexCoords.z += CurrentFrame;
    Color = gl_Color;

    pos_viewspace = (gl_ModelViewMatrix * gl_Vertex).xyz;

    vec3 binormal = cross(gl_Normal, vec3(1.0, 0.0, 0.0));
    n_viewspace   = normalize(gl_NormalMatrix * gl_Normal);
    t_viewspace   = normalize(mat3(gl_ModelViewMatrix) * cross(gl_Normal, binormal));
    b_viewspace   = normalize(mat3(gl_ModelViewMatrix) * binormal);
}
