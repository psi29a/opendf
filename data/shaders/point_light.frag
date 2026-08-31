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

    // Unity's built-in point light falloff, which is what Daggerfall Unity
    // gets for its dungeon lights: 1/(1 + 25*(d/r)^2). That never quite
    // reaches zero, so rescale it to hit exactly 0 at the radius the way the
    // attenuation texture Unity samples does -- a light must not leak past
    // the volume it claims, and must not pop when it ends.
    //
    // The obvious (1 - d^2/r^2)^2 is a much flatter curve: about 5x brighter
    // at half the radius, which reads as broad soft pools instead of the
    // tight bright cores classic-style lighting wants.
    float t2 = (dist*dist) / (light_radius*light_radius);
    float atten = clamp((1.0/(1.0 + 25.0*t2) - 1.0/26.0) * (26.0/25.0), 0.0, 1.0);
    if(atten <= 0.0)
        discard;

    // dist can be exactly zero -- the torch rides the camera, so a surface can
    // land on the light -- and atten is 1.0 there, so the discard above does
    // not catch it. GLSL 1.20 leaves 0.0/0.0 undefined, so clamp the divisor.
    vec3 lightDir_viewspace = toLight / max(dist, 1e-4);

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
