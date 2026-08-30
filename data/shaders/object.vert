#version 120

varying vec3 pos_viewspace;
varying vec3 n_viewspace;
varying vec3 t_viewspace;
varying vec3 b_viewspace;
varying vec4 TexCoords;
varying vec4 Color;

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    TexCoords = gl_MultiTexCoord0;
    Color = gl_Color;

    pos_viewspace = (gl_ModelViewMatrix * gl_Vertex).xyz;

    n_viewspace   = normalize(gl_NormalMatrix * gl_Normal);
    // meshmanager packs the binormal into texcoord unit 1 (see setTexCoordArray(1, ...))
    b_viewspace   = normalize(mat3(gl_ModelViewMatrix) * gl_MultiTexCoord1.xyz);
    t_viewspace   = cross(n_viewspace, b_viewspace);
}
