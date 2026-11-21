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
#include <map>

#include "mesh_data.hpp"
#include "obj_loaders.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace
{
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

    GLFWwindow* window = glfwCreateWindow(1920, 1080, "Sanctified Vidette", glfwGetPrimaryMonitor(), nullptr);

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

    const bool loaded =
        cache.add("bunny", "assets/models/bunny.obj", "assets/textures/beastfur.png") &&
        cache.add("teapot", "assets/models/teapot.obj", "assets/textures/lizard_skin.png") &&
        cache.add("cat", "assets/models/cat.obj", "assets/textures/cat.png") &&
        cache.add("fishClown", "assets/models/fishClown.obj", "assets/textures/clownfish.png", "assets/models/fishclown.mtl") &&
        cache.add("monkey", "assets/models/monkey.obj", "assets/textures/cat.png");

    if (!loaded)
    {
        std::cerr << "Required scene assets missing or invalid.\n";
        cache.clear();
        gl::glDeleteProgram(program);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // this should not read disk again
    cache.add("bunny", "assets/models/bunny.obj", "assets/textures/beastfur.png");

    Mesh floor = uploadMesh(makeFloor());
    gl::GLuint floorTexture = loadTexture("assets/textures/trak_tile_y.jpg");

    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460 core");

    // 0 - tinyobj
    // 1 - custom
    int selectedLoader = 0;
    // 0 - front
    // 1 - back
    // 2 - both
    int faceCullMode = 0;
    bool textured = true;
    bool showNormals = false;
    bool animate = true;
    bool reverseRotation = false;
    bool perspective = true;
    bool wireframe = false;
    bool shadows = true;
    bool previousP = false;
    bool previousW = false;
    bool cullFaces = false;

    float lightDirection[3] = {-0.5f, 0.9f, 0.7f};
    float cameraPos[3] = {7.5f, 7.0f, 12.0f};
    float cameraTarget[3] = {0.f, 0.f, 0.f};
    float cameraFOV = 70.f;
    float animationSpeedModifier = 1.0f;

    // name, pos, scale, rotrate
    const std::array<Instance, 5> instances =
    {{
        {"bunny", {-5.0f, kFloorY, -1.5f}, {1.f, 1.f, 1.f}, 0.0f},
        {"teapot", {-3.0f, kFloorY,  1.8f}, {0.02f, 0.02f, 0.02f}, 0.0f},
        {"cat", {-0.8f, kFloorY + 2, -1.6f}, {3.0f, 3.0f, 3.0f}, 3.f},
        {"fishClown", {2.2f, kFloorY + 1, 1.1f}, {10.0f, 10.0f, 10.0f}, 0.f},
        {"monkey", { 4.8f, kFloorY + 1.5, -1.4f}, {1.1f, 1.1f, 1.1f}, 0.0f}
    }};

    // 0 fill
    // 1 line
    // 2 point
    int currentMode = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        //imgui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::End();

        // OpenGL error checking
        gl::GLenum error;
        while ((error = gl::glGetError()) != gl::GL_NO_ERROR)
        {
            std::cerr << "OpenGL error: 0x" << std::hex << static_cast<unsigned int>(error) << std::dec << '\n';
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Clean up GPU resources
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    cache.clear();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
