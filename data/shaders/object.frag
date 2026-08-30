#version 120
#extension GL_EXT_texture_array : require
#extension GL_ARB_draw_buffers : require

uniform vec4 illumination_color;

uniform sampler2DArray diffuseTex;

varying vec3 pos_viewspace;
varying vec3 n_viewspace;
varying vec3 t_viewspace;
varying vec3 b_viewspace;
varying vec4 TexCoords;
varying vec4 Color;

void main()
{
    vec4 color = vec4(texture2DArray(diffuseTex, TexCoords.xyz).rgb, 0.0);
    vec4 nn = vec4(0.5, 0.5, 1.0, 1.0);

    mat3 nmat = mat3(normalize(t_viewspace),
                     normalize(b_viewspace),
                     normalize(n_viewspace));

    gl_FragData[0] = color * vec4(Color.rgb, 1.0);
    gl_FragData[1] = vec4(nmat*(nn.xyz - vec3(0.5)) + vec3(0.5), nn.w);
    gl_FragData[2] = vec4(pos_viewspace, gl_FragCoord.z);
    gl_FragData[3] = illumination_color;
}
