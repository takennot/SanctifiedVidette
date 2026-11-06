#version 460 core

in vec3 vNormal;
out vec4 FragColor;

uniform vec3 uColor;
uniform int uShadowPass;

void main()
{
    if (uShadowPass == 1)
    {
        FragColor = vec4(0.02, 0.03, 0.05, 0.52);
        return;
    }

    vec3 normal = normalize(vNormal);
    vec3 lightDirection = normalize(vec3(-0.5, 0.9, 0.7));

    float diffuse = max(dot(normal, lightDirection), 0.0);
    float brightness = 0.35 + 0.65 * diffuse;

    FragColor = vec4(uColor * brightness, 1.0);
}
