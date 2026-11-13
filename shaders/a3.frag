#version 460 core

in vec3 vWorldNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform vec3 uColor;
uniform int uShadowPass;
uniform int uUseTexture;
uniform int uShowNormals;
uniform sampler2D uAlbedo; // sample a 2D texture
uniform vec3 uLightDirection;

void main()
{
    if (uShadowPass == 1)
    {
        FragColor = vec4(0.025, 0.035, 0.055, 0.48);
        return;
    }

    // renormalize just in case
    vec3 normal = normalize(vWorldNormal);

    if (uShowNormals == 1)
    {
        FragColor = vec4(normal * 0.5 + 0.5, 1.0);
        return;
    }

    vec3 albedo = uColor;

    if(uUseTexture == 1)
    {
        albedo *= texture(uAlbedo, vTexCoord).rgb;
    }

    // 1 is full bright, 0.32 min bright
    // max to clamp negatives (imagine negative light lmao)
    float diffuse = max(dot(normal, normalize(uLightDirection)), 0.0);
    float brightness = 0.32 + 0.68 * diffuse;

    FragColor = vec4(albedo * brightness, 1.0);
}
