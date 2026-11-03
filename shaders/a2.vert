#version 460 core

// I think??
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vNormal;

void main()
{
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);

    // https://learnopengl.com/Advanced-Lighting/Normal-Mapping
    // model space to world space
    // inverse(uModel) = undo model transformation
    // transpose = make correct normal transformation
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uProjection * uView * worldPosition;
}
