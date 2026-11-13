#include "glbinding/gl/enum.h"
#include "glbinding/gl/functions.h"
#include "glbinding/gl/types.h"
#include <GLFW/glfw3.h>
#include <cstdint>
#include <glbinding/gl/gl.h>
#include <glbinding/glbinding.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <random>
#include <vector>

#include "mesh_data.hpp"
#include "obj_loaders.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace
{
    constexpr float kFloorY = -0.8f;

    struct Mesh
    {
        gl::GLuint vbo = 0, vao = 0, ebo = 0;
        gl::GLsizei indexCount = 0;
    };

    struct Uniforms
    {
        gl::GLint model;
        gl::GLint view;
        gl::GLint projection;
        gl::GLint color;
        gl::GLint shadowPass;
        gl::GLint useTexture;
        gl::GLint showNormals;
        gl::GLint lightDirection;
        gl::GLint albedo;
    };

    // represents one model name registered in the scene
    // 2 versions + 1 texture
    struct ModelAsset
    {
        Mesh custom;
        Mesh external;
        gl::GLuint texture = 0;
        std::string path;
        std::string error;
        // for imgui
        std::size_t customTriangles = 0;
        std::size_t externalTriangles = 0;
        bool customValid = false;
        bool externalValid = false;
    };

    // Read GLSL shader from disk
    std::string readFile(const std::string& path)
    {
        std::ifstream file(path);

        if (!file)
        {
            std::cerr << "Cannot open: " << path << '\n';
            return {};
        }

        std::ostringstream stream;
        stream << file.rdbuf();
        return stream.str();
    }

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
            gl::glDeleteShader(shader);
            return 0;
        }

        return shader;
    }

    // Create a GPU program from vertex and fragment shaders
    gl::GLuint createProgram(const std::string& vertexSource, const std::string& fragmentSource)
    {
        gl::GLuint vs = compileShader(gl::GL_VERTEX_SHADER, vertexSource);
        if (!vs) return 0;

        gl::GLuint fs = compileShader(gl::GL_FRAGMENT_SHADER, fragmentSource);
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
        // gl_texture_2d is 2d target, mip lvl 0, gpu format gl_rgba8
        // width 2, height 2, border 0, incoming pixel layout gl_rgba, data type uint
        gl::glTexImage2D(gl::GL_TEXTURE_2D, 0, gl::GL_RGBA8, 2, 2, 0, gl::GL_RGBA, gl::GL_UNSIGNED_BYTE, rgba.data());
        // minification filter to gl_nearest, zooming out samples closest existing pixel, gives sharp checker pixels
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MIN_FILTER, gl::GL_NEAREST);
        // magnification to same, zooming in also stays blocky
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MAG_FILTER, gl::GL_NEAREST);
        return texture;
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
        // Magnification filter `GL_LINEAR` blends nearby texels when enlarging the texture
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MAG_FILTER, gl::GL_LINEAR);
        // horizontal aka U, wraps outside 0 - 1
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_WRAP_S, gl::GL_REPEAT);
        // same as above but V vertical
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_WRAP_T, gl::GL_REPEAT);
        // opengl has its own copy atp, so free the img
        stbi_image_free(pixels);

        return texture;
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

    // All meshes and png textures get uploaded once on registration
    // this class owns all named models & obj text strings
    class AssetCache
    {
    public:
        // register one model, name is lookup key
        bool add(const std::string& name, const std::string& objPath, const std::string& texturePath)
        {
            // if assets_ already contains this name, do nothing, return success
            if (assets_.count(name))
            {
                return true;
            }

            // find or create entry in objText_
            auto [it, inserted] = objText_.try_emplace(objPath);

            if (inserted)
            {
                it->second = readFile(objPath);
                ++diskReads_; // counted per unique obj path
            }

            if (it->second.empty())
            {
                std::cerr << "Could not open or read OBJ: " << objPath << '\n';
                return false;
            }

            // Both loaders parse the same std::string, so no file reopening
            // these get separate MeshData
            const ObjLoadResult own = loadCustomObj(it->second);
            const ObjLoadResult external = loadTinyObj(it->second);
            ModelAsset asset;
            asset.path = objPath;

            if (own.ok())
            {
                asset.custom = uploadMesh(own.mesh);
                asset.customValid = asset.custom.vao != 0;
                // 3 indices per triangle so divide by 3 to get index count
                asset.customTriangles = own.mesh.indices.size() / 3;
                ++gpuUploads_;
            }
            else
            {
                asset.error += "Custom: " + own.error + "; ";
            }

            if (external.ok())
            {
                asset.external = uploadMesh(external.mesh);
                asset.externalValid = asset.external.vao != 0;
                asset.externalTriangles = external.mesh.indices.size() / 3;
                ++gpuUploads_;
            }
            else
            {
                asset.error += "TinyObj: " + external.error + "; ";
            }

            for (const auto& warning : own.warnings)
            {
                std::cerr << "[Custom " << name << "] " << warning << '\n';
            }

            for (const auto& warning : external.warnings)
            {
                std::cerr << "[TinyObj " << name << "] " << warning << '\n';
            }

            if (!asset.customValid && !asset.externalValid)
            {
                std::cerr << "OBJ failed: " << name << ": " << asset.error << '\n';
                return false;
            }

            asset.texture = loadTexture(texturePath);
            // insert model asset into the assets_ map under name
            assets_.emplace(name, asset);
            return true;
        }

        // Required: name + position + optional transform/tint.
        // This only updates uniforms/bindings and issues a draw call.
        // name is lookup key, selects model
        //
        void renderObj(const std::string& name, const glm::vec3& pos,
                       const Uniforms& u, bool customLoader, bool shadow,
                       bool textured, bool debugNormals,
                       float yRotation = 0.0f,
                       const glm::vec3& scale = glm::vec3(1.0f),
                       const glm::vec3& tint = glm::vec3(1.0f)) const
        {
            const auto it = assets_.find(name);

            if (it == assets_.end())
            {
                return;
            }

            const ModelAsset& asset = it->second;
            const Mesh* mesh = customLoader ? &asset.custom : &asset.external;
            // fallback to other version
            if (!mesh->vao)
            {
                mesh = customLoader ? &asset.external : &asset.custom;
            }
            // just in case if neither mesh has vao
            if (!mesh->vao)
            {
                return;
            }

            const glm::mat4 model = glm::scale(
                                    glm::rotate(glm::translate(glm::mat4(1.0f), pos),
                                                yRotation, glm::vec3(0, 1, 0)), scale);

            drawMesh(*mesh, u, model, tint, asset.texture, shadow, textured, debugNormals);
        }

        // number of first-seen OBJ file paths read
        int diskReads() const
        {
            return diskReads_;
        }

        // number of OBJ GPU mesh upload attempts counted at registration
        int gpuUploads() const
        {
            return gpuUploads_;
        }

        // number of registered names (not scene copies and not unique file paths)
        std::size_t uniqueModels() const
        {
            return assets_.size();
        }

        // imgui stuff
        const std::map<std::string, ModelAsset>& items() const
        {
            return assets_;
        }

        // release all cache-owned gpu resources while a valid OpenGL context still exists
        void clear()
        {
            for (auto& pair : assets_)
            {
                destroyMesh(pair.second.custom);
                destroyMesh(pair.second.external);

                if (pair.second.texture)
                {
                    gl::glDeleteTextures(1, &pair.second.texture);
                }
            }

            assets_.clear();
            objText_.clear();
        }

    private:
        // sorted mapping from model name to modelasset
        std::map<std::string, ModelAsset> assets_;
        // hash mapping from obj path to its text
        std::unordered_map<std::string, std::string> objText_;
        // attempts to read first-seen OBJ paths
        int diskReads_ = 0;
        // successful-parser GPU mesh upload attempts
        int gpuUploads_ = 0;
    };

    // uses UV now
    MeshData makeFloor()
    {
        MeshData md;
        const float x = 8.5f, z = 5.6f;
        // uv - 00, 80, 85, 05
        // 80, 85, 05 to repeat texture at x and/or z
        md.vertices = {
            {-x, kFloorY,  z, 0,1,0, 0,0},
            { x, kFloorY,  z, 0,1,0, 8,0},
            { x, kFloorY, -z, 0,1,0, 8,5},
            {-x, kFloorY, -z, 0,1,0, 0,5}
        };
        md.indices = {0, 1, 2, 0, 2, 3};
        return md;
    }

    // reworked SceneObject kinda
    // placement of a model in a scene
    struct Instance
    {
        const char* name;
        glm::vec3 position;
        glm::vec3 scale;
        float rotationSpeed;
    };

    void glfwErrorCallback(int code, const char* message)
    {
        std::cerr << "GLFW " << code << ": " << message << '\n';
    }
}

int main()
{
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // 4x msaa
    glfwWindowHint(GLFW_SAMPLES, 4);
    // 8 bit stencil buffer for shadows
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Sanctified Vidette", nullptr, nullptr);

    if (!window)
    {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    // vsync, swapping waits for monitor refresh
    glfwSwapInterval(1);

    // glbinding provides access to OpenGL functions
    glbinding::initialize(glfwGetProcAddress);

    // per pixel, keep only the closest thing
    gl::glEnable(gl::GL_DEPTH_TEST);
    // turn on aa
    gl::glEnable(gl::GL_MULTISAMPLE);

    // Load and compile GLSL
    std::string vertexSource = readFile("shaders/a3.vert");
    std::string fragmentSource = readFile("shaders/a3.frag");

    gl::GLuint program = createProgram(vertexSource, fragmentSource);

    if (!program)
    {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    gl::glUseProgram(program);

    const Uniforms u =
    {
        gl::glGetUniformLocation(program, "uModel"),
        gl::glGetUniformLocation(program, "uView"),
        gl::glGetUniformLocation(program, "uProjection"),
        gl::glGetUniformLocation(program, "uColor"),
        gl::glGetUniformLocation(program, "uShadowPass"),
        gl::glGetUniformLocation(program, "uUseTexture"),
        gl::glGetUniformLocation(program, "uShowNormals"),
        gl::glGetUniformLocation(program, "uLightDirection"),
        gl::glGetUniformLocation(program, "uAlbedo")
    };

    AssetCache cache;

    bool perspective = true;
    bool previousP = false;

    bool wireframe = false;
    bool previousW = false;

    tinyobj::attrib_t attributes;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warnings;
    std::string errors;
    std::string modelPath = "assets/bunny.obj";

    bool ret = tinyobj::LoadObj(&attributes, &shapes, &materials, &warnings, &errors, modelPath.c_str());

    if (!warnings.empty())
    {
      std::cout << warnings << std::endl;
    }

    if (!errors.empty())
    {
      std::cerr << errors << std::endl;
    }

    if (!ret)
    {
      exit(1);
    }


    MeshData bunnyData;

    for (const auto& shape : shapes)
    {
        const auto& indices = shape.mesh.indices;

        // TinyObjLoader triangulates faces by default.
        for (size_t j = 0; j + 2 < indices.size(); j += 3)
        {
            glm::vec3 positions[3];

            for (int k = 0; k < 3; ++k)
            {
                const auto& idx = indices[j + k];
                const size_t v = static_cast<size_t>(idx.vertex_index) * 3;

                positions[k] = {
                    attributes.vertices[v],
                    attributes.vertices[v + 1],
                    attributes.vertices[v + 2]
                };
            }

            // Fallback normal for OBJ files without normals
            glm::vec3 cross = glm::cross(
                positions[1] - positions[0],
                positions[2] - positions[0]
            );

            glm::vec3 faceNormal = glm::dot(cross, cross) > 1e-12f ? glm::normalize(cross) : glm::vec3(0.0f, 1.0f, 0.0f);

            const std::uint32_t start = static_cast<std::uint32_t>(bunnyData.vertices.size());

            for (int k = 0; k < 3; ++k)
            {
                const auto& idx = indices[j + k];
                glm::vec3 normal = faceNormal;

                if (idx.normal_index >= 0)
                {
                    const size_t n = static_cast<size_t>(idx.normal_index) * 3;

                    if (n + 2 < attributes.normals.size())
                    {
                        normal = {
                            attributes.normals[n],
                            attributes.normals[n + 1],
                            attributes.normals[n + 2]
                        };
                    }
                }

                addVertex(bunnyData, positions[k], normal);
            }

            bunnyData.indices.insert(
                bunnyData.indices.end(),
                {start, start + 1, start + 2}
            );
        }
    }

    const Mesh bunny = uploadMesh(bunnyData);

    // 0 fill
    // 1 line
    // 2 point
    int currentMode = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Toggle once per P key press
        const bool pressedP = glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS;

        if (pressedP && !previousP)
        {
            perspective = !perspective;
            glfwSetWindowTitle(window, perspective ? "Sanctified Vidette [Perspective]" : "Sanctified Vidette [Orthographic]");
        }
        previousP = pressedP;

        const bool pressedW = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;

        if (pressedW && !previousW)
        {
            wireframe = !wireframe;

            if (currentMode == 0) {
                currentMode = 1;
                gl::glPolygonMode(gl::GL_FRONT_AND_BACK, gl::GL_LINE);
            }
            else if (currentMode == 1) {
                currentMode = 2;
                gl::glPolygonMode(gl::GL_FRONT_AND_BACK, gl::GL_POINT);
            }
            else if (currentMode == 2) {
                currentMode = 0;
                gl::glPolygonMode(gl::GL_FRONT_AND_BACK, gl::GL_FILL);
            }

            //glPolygonMode(gl::GL_FRONT_AND_BACK, wireframe ? gl::GL_LINE : gl::GL_FILL);
        }
        previousW = pressedW;

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        int framebufferWidth, framebufferHeight;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

        gl::glViewport(0, 0, framebufferWidth, framebufferHeight);

        const float aspect = float(framebufferWidth) / float(framebufferHeight);

        // view matrix aka camera
        // look at args - camera pos, camera target dir, up
        const glm::mat4 view = glm::lookAt(
                               glm::vec3(7.5f, 7.0f, 12.0f),
                               glm::vec3(0.0f, 0.0f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));

        // projection matrix
        // 70 fov
        // perspective is weird - matrix copies depth to w coord. After vertex shader, gpu divides x & y by w to get that distortion.
        // ortho is weirder - box 10.4 tall and 10.4 * aspect wide, but this time no w division so kinda mapping straight to screen?
        const glm::mat4 projection = perspective
                                   ? glm::perspective(glm::radians(10.0f), aspect, 0.1f, 80.0f)
                                   : glm::ortho(-5.2f * aspect, 5.2f * aspect, -5.2f, 5.2f, 0.1f, 80.0f);

//         // clear last frame
        gl::glClearColor(0.09f, 0.12f, 0.20f, 1.0f);
        // gl::glClearStencil(0);
        gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT | gl::GL_STENCIL_BUFFER_BIT);

        // set view & projection, same for all objects anyway
        gl::glUseProgram(program);
        gl::glUniformMatrix4fv(u.view, 1, gl::GL_FALSE, glm::value_ptr(view));
        gl::glUniformMatrix4fv(u.projection, 1, gl::GL_FALSE, glm::value_ptr(projection));
        gl::glUniform1i(u.shadowPass, 0);

        const float t = static_cast<float>(glfwGetTime());

        // Prepare the six models & use same positions when drawing their shadows and the objects.
        // rotate first, then move
        const glm::mat4 spinningCube = glm::rotate(
                                       glm::translate(glm::mat4(1.0f), {-3.0f, 0.0f, 1.65f}),
                                       t, glm::vec3(0.0f, 1.0f, 0.0f));

        const glm::mat4 stillCube = glm::translate(glm::mat4(1.0f), {-3.0f, 0.0f, -1.65f});

        // use sin cos to get a smooooth wave
        const glm::mat4 movingSphere = glm::translate(
                                       glm::mat4(1.0f),
                                       {
                                           0.55f * std::sin(t),
                                           0.20f * std::sin(1.7f * t),
                                           1.65f + 0.45f * std::cos(t)
                                       });

        const glm::mat4 stillSphere = glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -1.65f});

        // sin is -1 to 1, so pulse is 0.6 to 1.1
        // First scale, then move
        const float pulse = 0.85f + 0.25f * std::sin(2.2f * t);
        const glm::mat4 pulsingPyramid = glm::scale(
                                         glm::translate(glm::mat4(1.0f), {3.0f, 0.0f, 1.65f}),
                                         glm::vec3(pulse));

        const glm::mat4 stillPyramid = glm::translate(glm::mat4(1.0f), {3.0f, 0.0f, -1.65f});

        // mesh pointer, matrix, color
        // const std::array<SceneObject, 6> objects = {{
        //     {&cube, spinningCube, {0.93f, 0.37f, 0.32f}},
        //     {&cube, stillCube, {0.31f, 0.64f, 0.91f}},
        //     {&sphere, movingSphere, {0.99f, 0.77f, 0.36f}},
        //     {&sphere, stillSphere, {0.36f, 0.82f, 0.66f}},
        //     {&pyramid, pulsingPyramid, {0.76f, 0.52f, 0.97f}},
        //     {&pyramid, stillPyramid, {0.96f, 0.57f, 0.33f}}
        // }};


        // floor is first so it writes depth & color
        // then shadows on top of floor + objects after can cover them
        // objects last

        // floor
        //drawMesh(floor, u, glm::mat4(1.0f), {0.40f, 0.46f, 0.55f});

        // shadows
//         // enable per pixel counter
//         gl::glEnable(gl::GL_STENCIL_TEST);
//         // write to all 8 bits
//         gl::glStencilMask(0xFF);
//         // pass only if counter is 0
//         gl::glStencilFunc(gl::GL_EQUAL, 0, 0xFF);
//         // If stencil fails: keep. If depth fails: keep. If both pass: add 1
//         gl::glStencilOp(gl::GL_KEEP, gl::GL_KEEP, gl::GL_INCR);
//         // Shadows don't write depth, so they can't block anything drawn later
//         gl::glDepthMask(gl::GL_FALSE);  // Keep the floor depth.
//         // for transparency
//         gl::glEnable(gl::GL_BLEND);
//         // shadow * alpha + floor * (1 - alpha)
//         // https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBlendFunc.xhtml
//         // "Transparency is best implemented using blend function (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)" - straight up yoinked from docs
//         // Example with red color - if floor is 0.4 in red value and shadow is 0.02 in red - 0.02 * 0.52 (because we set alpha to 0.52 in the frag shader) + 0.4 * 0.48 (because 100-52) = 0.2024
//         gl::glBlendFunc(gl::GL_SRC_ALPHA, gl::GL_ONE_MINUS_SRC_ALPHA);
//         gl::glUniform1i(u.shadowPass, 1);
//
//         for (const SceneObject& object : objects)
//         {
//             //drawMesh(*object.mesh, u, object.model, object.color);
//         }

        gl::glUniform1i(u.shadowPass, 0);
        gl::glDisable(gl::GL_BLEND);
        gl::glDisable(gl::GL_STENCIL_TEST);
        gl::glDepthMask(gl::GL_TRUE);

        // draw the objects
        // for (const SceneObject& object : objects)
        // {
        //     drawMesh(*object.mesh, u, object.model, object.color);
        // }

        drawMesh(bunny, u, glm::mat4(1.0f), glm::vec3(0.8f, 0.8f, 0.8f));

        // OpenGL error checking
        gl::GLenum error;
        while ((error = gl::glGetError()) != gl::GL_NO_ERROR)
        {
            std::cerr << "OpenGL error: 0x" << std::hex << static_cast<unsigned int>(error) << std::dec << '\n';
        }

        glfwSwapBuffers(window);
    }

    // Clean up GPU resources
    // destroyMesh(floor);
    // destroyMesh(pyramid);
    // destroyMesh(sphere);
    // destroyMesh(cube);
    destroyMesh(bunny);
    gl::glDeleteProgram(program);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
