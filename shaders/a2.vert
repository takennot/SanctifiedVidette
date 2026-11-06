#version 460 core

// I think??
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform int uShadowPass;

out vec3 vNormal;

void main()
{
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);

    if (uShadowPass == 1)
    {
        // Direction from surface TOWARDS the light
        // Negative direction casts shadows AWAY from the light
        vec3 toLight = normalize(vec3(-0.5, 0.9, 0.7));

        // pure bs
        // calculates distance along the shadow ray
        // basically if we have point P and direction to light at L and the floor height is Y
        // then we need to move away point AWAY from L until we get Y is same as floor
        // So Pshadow = P - dL. d is distance traveled along the normalized direction.
        // its -dL because shadows go FROM the light, meanwhile L is TO light.
        // anyway, floorY = Py - dLy becomes dLy = Py - floorY
        // And by doing that - d = Py - floorY / Ly. Dividing by Ly to account for ray's angle fml.
        const float floorY = -0.8;
        float distance = (worldPosition.y - floorY) / toLight.y;
        worldPosition.x -= toLight.x * distance;
        worldPosition.z -= toLight.z * distance;

        worldPosition.y = floorY + 0.003;
    }

    // inverse-tranpose to keep relationship between normal & tangent directions of surface.
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uProjection * uView * worldPosition;
}
