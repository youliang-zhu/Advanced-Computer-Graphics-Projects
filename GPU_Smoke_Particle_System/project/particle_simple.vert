#version 420

layout(location = 0) in vec4 particle_data;
uniform mat4 projectionMatrix;

void main()
{
    gl_Position = projectionMatrix * vec4(particle_data.xyz, 1.0);
    gl_PointSize = 3.0;
}