#include "gpu.hpp"

#include <glbinding/gl/gl.h>
#include <glbinding/gl/functions.h>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// Read GLSL shader from disk
std::string readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file)
    {
        std::cerr << "Cannot open: " << path << '\n';
        return {};
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

namespace
{
    // Compile one shader + report errors
    gl::GLuint compileShader(gl::GLenum type, const std::string& source)
    {
        gl::GLuint shader = gl::glCreateShader(type);

        const char* text = source.c_str();
        gl::glShaderSource(shader, 1, &text, nullptr);
        gl::glCompileShader(shader);

        gl::GLint success = 0;
        gl::glGetShaderiv(shader, gl::GL_COMPILE_STATUS, &success);

        if (!success)
        {
            char log[4096] = {};
            gl::glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::cerr << "Shader compilation failed:\n" << log << '\n';
            std::cerr << "Shader source:\n" << source << '\n';
            gl::glDeleteShader(shader);
            return 0;
        }

        return shader;
    }

    // Fallback texture is visible if a PNG cannot be found;
    // returns gpu texture id for 2x2 debug img
    gl::GLuint createFallbackTexture()
    {
        // 2x2 pixels * 4 channels (rgba)
        constexpr std::array<unsigned char, 16> rgba = {
            255, 0, 255, 255,   32, 32, 32, 255,
            32, 32, 32, 255,   255, 0, 255, 255
        };

        gl::GLuint texture = 0;
        gl::glGenTextures(1, &texture);
        gl::glBindTexture(gl::GL_TEXTURE_2D, texture);
        // upload texture to gpu
        // gl_texture_2d is 2d target, mip lvl 0 (full size img), gpu format gl_rgba8
        // width 2, height 2, border 0, incoming pixel layout gl_rgba, data type uint
        gl::glTexImage2D(gl::GL_TEXTURE_2D, 0, gl::GL_RGBA8, 2, 2, 0, gl::GL_RGBA, gl::GL_UNSIGNED_BYTE, rgba.data());
        // https://learnopengl.com/Getting-started/Textures (texture filtering)
        // minification filter to gl_nearest, zooming out samples closest existing pixel, gives sharp checker pixels
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MIN_FILTER, gl::GL_NEAREST);
        // magnification to same, zooming in also stays blocky
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MAG_FILTER, gl::GL_NEAREST);
        return texture;
    }
}
    // Create a GPU program from vertex and fragment shaders
    gl::GLuint createProgram(const std::string& vertexSource, const std::string& fragmentSource)
    {
        const std::string vsText = readFile(vertexSource);
        const std::string fsText = readFile(fragmentSource);

        if (vsText.empty() || fsText.empty())
        {
            std::cerr << "Missing shaders: " << vertexSource << " or " << fragmentSource << '\n';
            return 0;
        }

        gl::GLuint vs = compileShader(gl::GL_VERTEX_SHADER, vsText);

        if (!vs)
        {
            return 0;
        }

        gl::GLuint fs = compileShader(gl::GL_FRAGMENT_SHADER, fsText);

        if (!fs)
        {
            gl::glDeleteShader(vs);
            return 0;
        }

        gl::GLuint program = gl::glCreateProgram();
        gl::glAttachShader(program, vs);
        gl::glAttachShader(program, fs);
        gl::glLinkProgram(program);

        gl::GLint success = 0;
        gl::glGetProgramiv(program, gl::GL_LINK_STATUS, &success);

        if (!success)
        {
            char log[4096] = {};
            gl::glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            std::cerr << "Shader link failed:\n" << log << '\n';
            gl::glDeleteProgram(program);
            program = 0;
        }

        gl::glDeleteShader(vs);
        gl::glDeleteShader(fs);

        return program;
    }

    Uniforms findUniforms(gl::GLuint p)
    {
        return
        {
            gl::glGetUniformLocation(p, "uModel"),
            gl::glGetUniformLocation(p, "uView"),
            gl::glGetUniformLocation(p, "uProjection"),
            gl::glGetUniformLocation(p, "uColor"),
            gl::glGetUniformLocation(p, "uShadowPass"),
            gl::glGetUniformLocation(p, "uUseTexture"),
            gl::glGetUniformLocation(p, "uShowNormals"),
            gl::glGetUniformLocation(p, "uLightDirection"),
            gl::glGetUniformLocation(p, "uAlbedo")
        };
    }

    Mesh uploadMesh(const MeshData& meshData)
    {
        Mesh mesh;

        if (meshData.vertices.empty() || meshData.indices.empty())
        {
            return mesh;
        }

        // get handles, bind (make active) vao
        mesh.indexCount = static_cast<gl::GLsizei>(meshData.indices.size());
        gl::glGenVertexArrays(1, &mesh.vao);
        gl::glGenBuffers(1, &mesh.vbo);
        gl::glGenBuffers(1, &mesh.ebo);
        gl::glBindVertexArray(mesh.vao);

        // bind vbo, copy vertex bytes to gpu
        gl::glBindBuffer(gl::GL_ARRAY_BUFFER, mesh.vbo);
        gl::glBufferData(gl::GL_ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(meshData.vertices.size() * sizeof(Vertex)), meshData.vertices.data(), gl::GL_STATIC_DRAW);

        // same but ebo
        // apparently ebo binding gets remembered by vao, cool
        gl::glBindBuffer(gl::GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        gl::glBufferData(gl::GL_ELEMENT_ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(meshData.indices.size() * sizeof(std::uint32_t)), meshData.indices.data(), gl::GL_STATIC_DRAW);

        // attribute number (0/1 bc layout lcoation), how many components (3), type, normalize ints, stride (24 bytes from one vertex to next), offset
        gl::glVertexAttribPointer(0, 3, gl::GL_FLOAT, gl::GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, px)));
        gl::glEnableVertexAttribArray(0);
        gl::glVertexAttribPointer(1, 3, gl::GL_FLOAT, gl::GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, nx)));
        gl::glEnableVertexAttribArray(1);
        gl::glVertexAttribPointer(2, 2, gl::GL_FLOAT, gl::GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
        gl::glEnableVertexAttribArray(2);
        // unbind so nothing changes it, just in case, you know
        gl::glBindVertexArray(0);

        return mesh;
    }

    // helper, nuffsaid
    void destroyMesh(Mesh& m)
    {
        if (m.ebo)
        {
            gl::glDeleteBuffers(1, &m.ebo);
        }

        if (m.vbo)
        {
            gl::glDeleteBuffers(1, &m.vbo);
        }

        if (m.vao)
        {
            gl::glDeleteVertexArrays(1, &m.vao);
        }

        m = {};
    }

    // use stb_image to read img from disk, create opengl texture, return id
    gl::GLuint loadTexture(const std::string& path)
    {
        // obj vt (0,0) is bottom-left
        // img usually starts top-left
        stbi_set_flip_vertically_on_load(true);
        int width = 0, height = 0, channels = 0;
        // load, fill dimensions, use 4 channes
        unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);

        // check img
        if (!pixels || width <= 0 || height <= 0)
        {
            std::cerr << "Texture error: " << path << ": " << stbi_failure_reason() << '\n';
            stbi_image_free(pixels);
            return createFallbackTexture();
        }

        gl::GLuint texture = 0;
        gl::glGenTextures(1, &texture);
        gl::glBindTexture(gl::GL_TEXTURE_2D, texture);
        gl::glTexImage2D(gl::GL_TEXTURE_2D, 0, gl::GL_RGBA8, width, height, 0, gl::GL_RGBA, gl::GL_UNSIGNED_BYTE, pixels);
        // distant objs avoid excessive aliasing
        gl::glGenerateMipmap(gl::GL_TEXTURE_2D);
        // interpolates between nearby texels and between mip levels (trilinear filtering).
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MIN_FILTER, gl::GL_LINEAR_MIPMAP_LINEAR);
        // Magnification filter 'GL_LINEAR' blends nearby texels when enlarging the texture
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MAG_FILTER, gl::GL_LINEAR);
        // horizontal aka U, wraps outside 0 - 1
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_WRAP_S, gl::GL_REPEAT);
        // same as above but V vertical
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_WRAP_T, gl::GL_REPEAT);
        // opengl has its own copy atp, so free the img
        stbi_image_free(pixels);

        return texture;
    }

    void destroyTexture(gl::GLuint& texture)
    {
        if (texture)
        {
            gl::glDeleteTextures(1, &texture);
        }

        texture = 0;
    }

    void drawMesh(const Mesh& mesh, const Uniforms& u, const glm::mat4& model,
                     const glm::vec3& tint, gl::GLuint texture, bool shadow,
                     bool useTexture, bool showNormals)
    {
        // glDrawElements: draw triangles, using indexCount indices of type uint, starting at offset 0 of the bound EBO.
        // args - location (u.model), count (1 matrix), transpose (no), float pointer value
        // no need to transpose because glm & opengl are both column major matrices
        if (!mesh.vao)
        {
            return;
        }

        gl::glUniformMatrix4fv(u.model, 1, gl::GL_FALSE, glm::value_ptr(model));
        gl::glUniform3f(u.color, tint.r, tint.g, tint.b);
        gl::glUniform1i(u.shadowPass, shadow ? 1 : 0);
        gl::glUniform1i(u.useTexture, useTexture ? 1 : 0);
        gl::glUniform1i(u.showNormals, showNormals ? 1 : 0);
        // select texture unit 0
        gl::glActiveTexture(gl::GL_TEXTURE0);
        // bind this model's texture id on unit 0, so uAlbedo=0 can sample it
        gl::glBindTexture(gl::GL_TEXTURE_2D, texture);
        gl::glBindVertexArray(mesh.vao);
        // draw mode, count, type, offset
        gl::glDrawElements(gl::GL_TRIANGLES, mesh.indexCount, gl::GL_UNSIGNED_INT, nullptr);
    }
