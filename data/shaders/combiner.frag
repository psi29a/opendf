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

    // Lighting was done in linear space (see the albedo decode in
    // object/sprite/terrain.frag); encode back to sRGB for display. Without
    // this every lit surface reads far darker than it should, and a physically
    // shaped falloff looks broken while a flat one looks "right" by accident.
    vec3 lit = color*diffuse + specular;
    gl_FragColor = vec4(pow(max(lit, vec3(0.0)), vec3(1.0/2.2)), 1.0);
}
