#version 120
#extension GL_ARB_texture_rectangle : enable
#extension GL_ARB_draw_buffers : require

varying vec3 lightPos_viewspace;

uniform vec4 diffuse_color;
uniform vec4 specular_color;
uniform float light_radius;

uniform sampler2DRect ColorTex;
uniform sampler2DRect NormalTex;
uniform sampler2DRect PosTex;

void main()
{
    vec4 c_viewspace = texture2DRect(ColorTex,  gl_FragCoord.xy);
    vec3 n_viewspace = texture2DRect(NormalTex, gl_FragCoord.xy).xyz*2.0 - vec3(1.0);
    vec3 p_viewspace = texture2DRect(PosTex,    gl_FragCoord.xy).xyz;

    // Direction from point to light (not vice versa!)
    vec3 toLight = lightPos_viewspace - p_viewspace;
    float dist = length(toLight);

    // Falls to exactly zero at the light's radius (so a light never leaks past
    // the volume it claims and never pops), but stays much brighter near the
    // source than a plain linear ramp would.
    float atten = clamp(1.0 - (dist*dist)/(light_radius*light_radius), 0.0, 1.0);
    atten *= atten;
    if(atten <= 0.0)
        discard;

    vec3 lightDir_viewspace = toLight / dist;

    // Lambertian diffuse color. No ambient term here -- the lighting pass
    // blends GL_ONE/GL_ONE, and ambient is contributed once by dir_light.
    vec3 diff = diffuse_color.rgb * max(0.0, dot(lightDir_viewspace, n_viewspace)) * atten;

    // Direction from point to camera.
    vec3 viewDir_viewspace = normalize(-p_viewspace);

    // Blinn-Phong specular highlights.
    vec3 h_viewspace = normalize(lightDir_viewspace + viewDir_viewspace);
    float amount = max(0.0, dot(h_viewspace, n_viewspace));
    vec3 spec = specular_color.rgb * pow(amount, 32.0) * c_viewspace.a * atten;

    gl_FragData[0] = vec4(diff, 1.0);
    gl_FragData[1] = vec4(spec, 1.0);
}
