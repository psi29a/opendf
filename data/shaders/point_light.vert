#version 120

uniform vec3 light_position;

varying vec3 lightPos_viewspace;

void main()
{
    // The light pass inherits the main camera's view matrix (it's a
    // RELATIVE_RF camera), so this puts the world-space light position into
    // the same view space the G-buffer's position texture is stored in.
    lightPos_viewspace = (gl_ModelViewMatrix * vec4(light_position, 1.0)).xyz;

    // Vertex position in main camera Screen space.
    gl_Position = gl_ProjectionMatrix * gl_Vertex;
}
