#version 120

varying vec4 Color;
varying vec4 TexCoord0;

void main()
{
    gl_Position = gl_ProjectionMatrix * gl_Vertex;
    TexCoord0 = gl_MultiTexCoord0;
    Color = gl_Color;
}
