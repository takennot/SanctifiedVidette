#include "glbinding/gl/functions.h"
#include "glbinding/gl/types.h"
#include <GLFW/glfw3.h>
#include <cstdint>
#include <glbinding/gl/gl.h>
#include <glbinding/glbinding.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <random>
#include <vector>

// p - position
// n - normal (direction)
struct Vertex
{
    float px, py, pz;
    float nx, ny, nz;
};

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

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
};

struct Color
{
    float r, g, b;
};

struct SceneObject
{
    const Mesh* mesh;
    glm::mat4 model;
    glm::vec3 color;
};

// Read GLSL shader from disk
std::string readFile(const char* path)
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
        char log[2048] = {};
        gl::glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Shader error:\n" << log << '\n';
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
        char log[2048] = {};
        gl::glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "Program error:\n" << log << '\n';
    }

    gl::glDeleteShader(vs);
    gl::glDeleteShader(fs);

    if (!success)
    {
        gl::glDeleteProgram(program);
        return 0;
    }

    return program;
}

// helper
// converts vec3s to vertex + append
void addVertex(MeshData& meshData, glm::vec3 p, glm::vec3 n)
{
    meshData.vertices.push_back({p.x, p.y, p.z, n.x, n.y, n.z});
}

// b - a is the edge from a to b.
// c - a is the edge from a to c.
// Their cross product points out of the face.
// Normalize makes it length 1.
// Every vertex gets same normal because its same face bruh
// Why duplicate vertices? Because im stupid, thats why. Cube corner has 3 different face directions, so 3 normals. 1 Vertex = 1 normal. So duplicate that shit.
void addQuad(MeshData& meshData, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d)
{
    const glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
    const std::uint32_t start = static_cast<std::uint32_t>(meshData.vertices.size());
    addVertex(meshData, a, n);
    addVertex(meshData, b, n);
    addVertex(meshData, c, n);
    addVertex(meshData, d, n);
    meshData.indices.insert(meshData.indices.end(),
    {
        start, start + 1, start + 2,
        start, start + 2, start + 3
    });
}

// should be almost same as addQuad but -1 arg
void addTriangle(MeshData& meshData, glm::vec3 a, glm::vec3 b, glm::vec3 c)
{
    const glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
    const std::uint32_t start = static_cast<std::uint32_t>(meshData.vertices.size());
    addVertex(meshData, a, n);
    addVertex(meshData, b, n);
    addVertex(meshData, c, n);
    meshData.indices.insert(meshData.indices.end(),
    {
        start, start + 1, start + 2
    });
}

// s is half size
// centered on the origin
MeshData makeCube()
{
    MeshData md;
    const float s = 0.65f;
    // Ty Martin for leaking the cube :)
    addQuad(md, {-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s}); // front
    addQuad(md, { s,-s,-s}, {-s,-s,-s}, {-s, s,-s}, { s, s,-s}); // back
    addQuad(md, { s,-s, s}, { s,-s,-s}, { s, s,-s}, { s, s, s}); // right
    addQuad(md, {-s,-s,-s}, {-s,-s, s}, {-s, s, s}, {-s, s,-s}); // left
    addQuad(md, {-s, s, s}, { s, s, s}, { s, s,-s}, {-s, s,-s}); // top
    addQuad(md, {-s,-s,-s}, { s,-s,-s}, { s,-s, s}, {-s,-s, s}); // bottom

    return md;
}

MeshData makePyramid()
{
    MeshData md;
    const float s = 0.65f;
    const glm::vec3 tip{0.0f, 0.85f, 0.0f};
    const glm::vec3 a{-s, -s, s};
    const glm::vec3 b{s, -s, s};
    const glm::vec3 c{s, -s, -s};
    const glm::vec3 d{-s, -s, -s};
    addTriangle(md, a, b, tip);
    addTriangle(md, b, c, tip);
    addTriangle(md, c, d, tip);
    addTriangle(md, d, a, tip);
    addQuad(md, d, c, b, a); // bottom face reversed so that normal points down instead of up

    return md;
}

// OOOOOO CHECK THIS OUT: https://www.songho.ca/opengl/gl_sphere.html
// theta is angle down from north pole, 0 to pi
// phi is angle around vertical axis, 0 to 2pi
// cos(theta) is height(y), sin(theta) is how far from vertical axis we are (ring radius), cos(phi) & sin(phi) spread around the ring. Result should always have length 1.
// Vertices are stored by stacks, each stack having slices + 1 vertices. So vertex (stack y, slice x) has index y * (slices + 1) + x
//
MeshData makeSphere(int stacks = 24, int slices = 32)
{
    MeshData md;
    constexpr float pi = 3.14159265358979323846f;
    const float radius = 0.7f;

    // because loops use <= stacks/slices, there should be (stacks+1) * (slices+1) vertices

    for (int y = 0; y <= stacks; ++y)
    {
        // y / stacks = fraction from 0 to 1, to sweep evenly
        const float theta = pi * float(y) / float(stacks);
        for (int x = 0; x <= slices; ++x)
        {
            const float phi = 2.0f * pi * float(x) / float(slices);
            // cos(theta) is y aka height. 0 to 1
            // sin(theta) is how wide the stack is at that height. 0 to 1
            // Where on the stack - if stack is just a circle and circle is (cos(phi), sin(phi)), then multiply by the stack size
            // x = sin(theta)*cos(phi)
            // z = sin(theta)*sin(phi)
            const glm::vec3 normal
            {
                std::sin(theta) * std::cos(phi),
                std::cos(theta),
                std::sin(theta) * std::sin(phi)
            };
            // On a sphere the direction from the center to a surface point is the outward normal
            // Position is that normal stretched to radius
            addVertex(md, radius * normal, normal);
        }
    }

    // if a is current vertex
    // b is a + (slices + 1) aka vertex directly below the current one
    // a (top left), a+1 (top right), b (bottom left), b+1 (bottom right)
    // index = y * (slices + 1) + x
    for (int y = 0; y < stacks; ++y)
    {
        for (int x = 0; x < slices; ++x)
        {
            const std::uint32_t a = std::uint32_t(y * (slices + 1) + x);
            const std::uint32_t b = a + std::uint32_t(slices + 1);
            md.indices.insert(md.indices.end(),
            {
                a, a + 1, b,
                a + 1, b + 1, b
            });
        }
    }

    return md;
}

// Do I REALLY have to explain this???
MeshData makeFloor()
{
    MeshData md;
    const float y = -0.8f;
    addQuad(md, {-5.3f,y, 4.0f}, { 5.3f,y, 4.0f}, { 5.3f,y,-4.0f}, {-5.3f,y,-4.0f});
    return md;
}

Mesh uploadMesh(const MeshData& meshData)
{
    Mesh mesh;

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
    gl::glVertexAttribPointer(0, 3, gl::GL_FLOAT, gl::GL_FALSE, sizeof(Vertex), nullptr);
    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(1, 3, gl::GL_FLOAT, gl::GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(3 * sizeof(float)));
    gl::glEnableVertexAttribArray(1);
    // unbind so nothing changes it, just in case, you know
    gl::glBindVertexArray(0);

    return mesh;
}

// helper, nuffsaid
void destroyMesh(const Mesh& m)
{
    gl::glDeleteBuffers(1, &m.ebo);
    gl::glDeleteBuffers(1, &m.vbo);
    gl::glDeleteVertexArrays(1, &m.vao);
}

void drawMesh(const Mesh& mesh, const Uniforms& u, const glm::mat4& model, const glm::vec3& color)
{
    // glDrawElements: draw triangles, using indexCount indices of type uint, starting at offset 0 of the bound EBO.
    // args - location (u.model), count (1 matrix), transpose (no), float pointer value
    // no need to transpose because glm & opengl are both column major matrices
    gl::glUniformMatrix4fv(u.model, 1, gl::GL_FALSE, glm::value_ptr(model));
    gl::glUniform3f(u.color, color.r, color.g, color.b);
    gl::glBindVertexArray(mesh.vao);
    // draw mode, count, type, offset
    gl::glDrawElements(gl::GL_TRIANGLES, mesh.indexCount, gl::GL_UNSIGNED_INT, nullptr);
}

// reserved for future :)
std::string randomString(size_t length)
{
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(32, 126);

    std::string result;
    result.reserve(length);

    for (size_t i = 0; i < length; i++)
    {
        result += static_cast<char>(dist(rng));
    }

    return result;
}

// reserved for future :)
Color randomColor()
{
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    return {dist(rng), dist(rng), dist(rng)};
}

int main()
{
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // 4x msaa
    glfwWindowHint(GLFW_SAMPLES, 4);
    // 8 bit stencil buffer for shadows
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1000, 1000, "Sanctified Vidette", nullptr, nullptr);

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
    std::string vertexSource = readFile("shaders/a2.vert");
    std::string fragmentSource = readFile("shaders/a2.frag");

    gl::GLuint program = createProgram(vertexSource, fragmentSource);

    if (!program)
    {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    gl::glUseProgram(program);

    Uniforms u
    {
        gl::glGetUniformLocation(program, "uModel"),
        gl::glGetUniformLocation(program, "uView"),
        gl::glGetUniformLocation(program, "uProjection"),
        gl::glGetUniformLocation(program, "uColor"),
        gl::glGetUniformLocation(program, "uShadowPass")
    };

    const Mesh cube = uploadMesh(makeCube());
    const Mesh sphere = uploadMesh(makeSphere());
    const Mesh pyramid = uploadMesh(makePyramid());
    const Mesh floor = uploadMesh(makeFloor());

    bool perspective = true;
    bool previousP = false;

    bool wireframe = false;
    bool previousW = false;

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
            glPolygonMode(gl::GL_FRONT_AND_BACK, wireframe ? gl::GL_LINE : gl::GL_FILL);
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
                                   ? glm::perspective(glm::radians(70.0f), aspect, 0.1f, 80.0f)
                                   : glm::ortho(-5.2f * aspect, 5.2f * aspect, -5.2f, 5.2f, 0.1f, 80.0f);

        // clear last frame
        gl::glClearColor(0.09f, 0.12f, 0.20f, 1.0f);
        gl::glClearStencil(0);
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
        const std::array<SceneObject, 6> objects = {{
            {&cube, spinningCube, {0.93f, 0.37f, 0.32f}},
            {&cube, stillCube, {0.31f, 0.64f, 0.91f}},
            {&sphere, movingSphere, {0.99f, 0.77f, 0.36f}},
            {&sphere, stillSphere, {0.36f, 0.82f, 0.66f}},
            {&pyramid, pulsingPyramid, {0.76f, 0.52f, 0.97f}},
            {&pyramid, stillPyramid, {0.96f, 0.57f, 0.33f}}
        }};


        // floor is first so it writes depth & color
        // then shadows on top of floor + objects after can cover them
        // objects last

        // floor
        drawMesh(floor, u, glm::mat4(1.0f), {0.40f, 0.46f, 0.55f});

        // shadows
        // enable per pixel counter
        gl::glEnable(gl::GL_STENCIL_TEST);
        // write to all 8 bits
        gl::glStencilMask(0xFF);
        // pass only if counter is 0
        gl::glStencilFunc(gl::GL_EQUAL, 0, 0xFF);
        // If stencil fails: keep. If depth fails: keep. If both pass: add 1
        gl::glStencilOp(gl::GL_KEEP, gl::GL_KEEP, gl::GL_INCR);
        // Shadows don't write depth, so they can't block anything drawn later
        gl::glDepthMask(gl::GL_FALSE);  // Keep the floor depth.
        // for transparency
        gl::glEnable(gl::GL_BLEND);
        // shadow * alpha + floor * (1 - alpha)
        // https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBlendFunc.xhtml
        // "Transparency is best implemented using blend function (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)" - straight up yoinked from docs
        // Example with red color - if floor is 0.4 in red value and shadow is 0.02 in red - 0.02 * 0.52 (because we set alpha to 0.52 in the frag shader) + 0.4 * 0.48 (because 100-52) = 0.2024
        gl::glBlendFunc(gl::GL_SRC_ALPHA, gl::GL_ONE_MINUS_SRC_ALPHA);
        gl::glUniform1i(u.shadowPass, 1);

        for (const SceneObject& object : objects)
        {
            drawMesh(*object.mesh, u, object.model, object.color);
        }

        gl::glUniform1i(u.shadowPass, 0);
        gl::glDisable(gl::GL_BLEND);
        gl::glDisable(gl::GL_STENCIL_TEST);
        gl::glDepthMask(gl::GL_TRUE);

        // draw the objects
        for (const SceneObject& object : objects)
        {
            drawMesh(*object.mesh, u, object.model, object.color);
        }

        // OpenGL error checking
        gl::GLenum error;
        while ((error = gl::glGetError()) != gl::GL_NO_ERROR)
        {
            std::cerr << "OpenGL error: 0x" << std::hex << static_cast<unsigned int>(error) << std::dec << '\n';
        }

        glfwSwapBuffers(window);
    }

    // Clean up GPU resources
    destroyMesh(floor);
    destroyMesh(pyramid);
    destroyMesh(sphere);
    destroyMesh(cube);
    gl::glDeleteProgram(program);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
