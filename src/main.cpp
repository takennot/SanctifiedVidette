#include <GLFW/glfw3.h>
#include <glbinding/gl/gl.h>
#include <glbinding/glbinding.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <random>
#include <cstdint>
#include <memory>

constexpr std::size_t RAM_SIZE = 16ULL * 1024 * 1024 * 1024;

struct Color
{
    float r, g, b;
};

struct Uniforms
{
    gl::GLint position;
    gl::GLint scale;
    gl::GLint rotation;
    gl::GLint color;
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

// Draw the original triangle, transformed using uniforms
void drawTriangle(const Uniforms& u, float x, float y, float sx, float sy, float rotation, Color color)
{
    gl::glUniform2f(u.position, x, y);
    gl::glUniform2f(u.scale, sx, sy);
    gl::glUniform1f(u.rotation, rotation);
    gl::glUniform3f(u.color, color.r, color.g, color.b);

    gl::glDrawArrays(gl::GL_TRIANGLES, 0, 3);
}

// Draw a rectangle with 2 draws of the same triangle
void drawRectangle(const Uniforms& u, float x, float y, float width, float height, Color color)
{
    gl::glUniform1f(u.rotation, 0.0f);
    gl::glUniform3f(u.color, color.r, color.g, color.b);

    // Lower left triangle
    gl::glUniform2f(u.position, x, y);
    gl::glUniform2f(u.scale, width, height);
    gl::glDrawArrays(gl::GL_TRIANGLES, 0, 3);

    // Upper right triangle
    gl::glUniform2f(u.position, x + width, y + height);
    gl::glUniform2f(u.scale, -width, -height); // flip
    gl::glDrawArrays(gl::GL_TRIANGLES, 0, 3);
}

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
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(1000, 1000, "Sanctified Vidette", nullptr, nullptr);

    if (!window)
    {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // glbinding provides access to OpenGL functions
    glbinding::initialize(glfwGetProcAddress);

    // Load and compile GLSL
    std::string vertexSource = readFile("shaders/house.vert");
    std::string fragmentSource = readFile("shaders/house.frag");

    gl::GLuint program = createProgram(vertexSource, fragmentSource);

    if (!program)
    {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    gl::glUseProgram(program);

    Uniforms u{
        gl::glGetUniformLocation(program, "uPosition"),
        gl::glGetUniformLocation(program, "uScale"),
        gl::glGetUniformLocation(program, "uRotation"),
        gl::glGetUniformLocation(program, "uColor")
    };

    // geometry buffer
    // A right triangle in local coordinates
    const float triangle[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f
    };

    gl::GLuint vao = 0;
    gl::GLuint vbo = 0;

    gl::glGenVertexArrays(1, &vao);
    gl::glGenBuffers(1, &vbo);

    gl::glBindVertexArray(vao);
    gl::glBindBuffer(gl::GL_ARRAY_BUFFER, vbo);

    // Upload triangle data once
    gl::glBufferData(gl::GL_ARRAY_BUFFER, sizeof(triangle), triangle, gl::GL_STATIC_DRAW);

    gl::glVertexAttribPointer(0, 2, gl::GL_FLOAT, gl::GL_FALSE, 2 * sizeof(float), nullptr);

    gl::glEnableVertexAttribArray(0);

    // Colors yay
    const Color wallColor   {0.82f, 0.49f, 0.32f};
    const Color windowColor {0.35f, 0.76f, 0.91f};
    const Color doorColor   {0.33f, 0.23f, 0.18f};

    const Color roofRed     {0.75f, 0.20f, 0.24f};
    const Color roofBlue    {0.20f, 0.35f, 0.78f};

    bool alternateRoof = false;
    bool previousSpace = false;


    //  RAM
    auto wastedRAM = std::make_unique_for_overwrite<std::uint8_t[]>(RAM_SIZE);
    volatile std::uint8_t* ptr = wastedRAM.get();
    for (std::size_t i = 0; i < RAM_SIZE; i += 4096)
    {
        ptr[i] = 1;
    }

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Toggle once per Space key press
        bool currentSpace = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;

        if (currentSpace && !previousSpace)
        {
            alternateRoof = !alternateRoof;
        }

        previousSpace = currentSpace;

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        int framebufferWidth, framebufferHeight;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

        gl::glViewport(0, 0, framebufferWidth, framebufferHeight);

        gl::glClearColor(0.78f, 0.88f, 0.96f, 1.0f);
        gl::glClear(gl::GL_COLOR_BUFFER_BIT);

        gl::glUseProgram(program);
        gl::glBindVertexArray(vao);

        // House body: 2 triangles
        drawRectangle(u, -0.50f, -0.72f, 1.00f, 1.04f, wallColor);

        // Roof: 2 triangles merged into one big triangle
        Color roofColor = alternateRoof ? roofBlue : roofRed;

        drawTriangle(u, 0.0f, 0.32f, -0.60f, 0.43f, 0.0f, roofColor);

        drawTriangle(u, 0.0f, 0.32f, 0.60f, 0.43f, 0.0f, roofColor);

        // Left window: 2 triangles
        drawRectangle(u, -0.36f, -0.13f, 0.22f, 0.25f, windowColor);

        // Right window: 2 triangles
        drawRectangle(u, 0.14f, -0.13f, 0.22f, 0.25f, windowColor);

        // Door: 2 triangles
        drawRectangle(u, -0.12f, -0.72f, 0.24f, 0.50f, doorColor);

        std::string title = randomString(30);
        glfwSetWindowTitle(window, title.c_str());

        // OpenGL error checking
        gl::GLenum error;
        while ((error = gl::glGetError()) != gl::GL_NO_ERROR)
        {
            std::cerr << "OpenGL error: 0x" << std::hex << static_cast<unsigned int>(error) << std::dec << '\n';
        }

        glfwSwapBuffers(window);
    }

    // Clean up GPU resources
    gl::glDeleteBuffers(1, &vbo);
    gl::glDeleteVertexArrays(1, &vao);
    gl::glDeleteProgram(program);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
