#version 120

uniform sampler2D TexImage;

varying vec4 Color;
varying vec4 TexCoord0;

void main()
{
    gl_FragColor = texture2D(TexImage, TexCoord0.xy) * Color;
}
