#version 460 core

in vec3 vNormal;
out vec4 FragColor;

uniform vec3 uColor;

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 lightDirection = normalize(vec3(-0.5, 0.9, 0.7));

    float diffuse = max(dot(normal, lightDirection), 0.0);
    float brightness = 0.35 + 0.65 * diffuse;

    FragColor = vec4(uColor * brightness, 1.0);
}
