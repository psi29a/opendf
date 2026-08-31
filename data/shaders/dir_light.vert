#version 120

uniform vec3 light_direction;

varying vec3 localLightDir;

void main()
{
    localLightDir = mat3(gl_ModelViewMatrix) * light_direction;

    // Vertex position in main camera Screen space.
    gl_Position = gl_ProjectionMatrix * gl_Vertex;
}
