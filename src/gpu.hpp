#pragma once

#include "mesh_data.hpp"
#include <glbinding/gl/types.h>
#include <glm/glm.hpp>

struct GPU_Mesh
{
    gl::GLuint vbo = 0, vao = 0, ebo = 0;
    gl::GLsizei indexCount = 0;
};

struct Uniforms
{
    gl::GLint model = -1;
    gl::GLint view = -1;
    gl::GLint projection = -1;
    gl::GLint color = -1;
    gl::GLint shadowPass = -1;
    gl::GLint useTexture = -1;
    gl::GLint showNormals = -1;
    gl::GLint lightDirection = -1;
    gl::GLint albedo = -1;
};

std::string readFile(const std::string& path);

gl::GLuint createProgram(const std::string& vertFile, const std::string& fragFile);

Uniforms findUniforms(gl::GLuint program);

GPU_Mesh uploadMesh(const MeshData& data);

void destroyMesh(GPU_Mesh& mesh);

gl::GLuint loadTexture(const std::string& path);

void destroyTexture(gl::GLuint& texture);

void drawMesh(const GPU_Mesh& mesh, const Uniforms& u, const glm::mat4& model,
                 const glm::vec3& tint, gl::GLuint texture, bool shadow,
                 bool textured, bool showNormals);
