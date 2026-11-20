#pragma once

#include "input_state.hpp"
#include "input_actions.hpp"
#include <array>

struct GLFWwindow;

// glfw callbacks --> input state --> input actions --> action requests


// Commands express what player wants to do, not the actual movement or rendering
// for example: "Player 1 wants to move forward" or "look right by 90 degrees"
struct PlayerCommand
{
    float forward = 0.0f;   // +1 forward, -1 backward
    float right = 0.0f;     // +1 right, -1 left
    float up = 0.0f;        // +1 ascend, -1 descend
    float lookYaw = 0.0f;   // degrees to turn this frame
    float lookPitch = 0.0f; // degrees to look up this frame
    bool sprint = false;
};

struct FrameCommands
{
    std::array<PlayerCommand, 2> player{};
};

class InputSystem
{
    public:
        // Install GLFW callbacks BEFORE ImGui_ImplGlfw_InitForOpenGL(window, true).
        // ImGui chains to our callbacks, so both systems receive the same events.
        void attach(GLFWwindow* window);

        // Call AFTER ImGui_ImplGlfw_Shutdown(), before destroying the GLFW window.
        void detach(GLFWwindow* window);

        // Builds commands from recorded keyboard/mouse events. Gamepad is polled.
        FrameCommands buildCommands(GLFWwindow* window, float deltaSeconds, bool multiplayer, bool uiKeyboard, bool uiMouse, bool allowGamepad);

        // Subscribe to named press/release actions, separate from held-key movement.
        InputActions& actions()
        {
            return actions_;
        }

        bool hasGamepad() const;
        void releaseMouse(GLFWwindow* window);

    private:
        static void onKey(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void onMouseButton(GLFWwindow* window, int button, int action, int mods);
        static void onCursorPosition(GLFWwindow* window, double x, double y);
        static void onFocus(GLFWwindow* window, int focused);

        InputState state_;
        InputActions actions_;
        bool mouseLooking_ = false;
};
