#version 460 core

out vec4 FragColor; // RGBA output

uniform vec3 uColor; // RGB from C++

void main()
{
    FragColor = vec4(uColor, 1.0); // set color with alpha 1
}
