#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect ColorTex;
uniform sampler2DRect DiffuseTex;
uniform sampler2DRect SpecularTex;

varying vec4 TexCoord0;

void main()
{
    vec3 color = texture2DRect(ColorTex, TexCoord0.xy).rgb;
    vec3 diffuse = texture2DRect(DiffuseTex, TexCoord0.xy).rgb;
    vec3 specular = texture2DRect(SpecularTex, TexCoord0.xy).rgb;

    gl_FragColor = vec4(color*diffuse + specular, 1.0);
}
