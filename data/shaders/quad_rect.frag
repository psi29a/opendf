#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect TexImage;

varying vec4 TexCoord0;

void main()
{
    gl_FragColor = texture2DRect(TexImage, TexCoord0.xy);
}
