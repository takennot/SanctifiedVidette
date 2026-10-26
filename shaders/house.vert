#version 460 core

layout (location = 0) in vec2 aPosition; // read 2 floats per vertex from vertex attr 0

uniform vec2 uPosition;
uniform vec2 uScale;
uniform float uRotation; // radians

void main()
{
    // apply scale
    vec2 scaled = aPosition * uScale;

    // apply rotation
    // x^2 + y^2 = 1
    float c = cos(uRotation);
    float s = sin(uRotation);
    vec2 rotated = vec2(c * scaled.x - s * scaled.y, s * scaled.x + c * scaled.y);

    // move vertex to pos
    gl_Position = vec4(rotated + uPosition, 0.0, 1.0);
}
