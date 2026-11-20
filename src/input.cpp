#include "input.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>


// keyboard / mouse / controller --> glfw callbacks --> state_ --> buildcommands --> FrameCommands?
namespace
{
    float deadzone(float v)
    {
        // Ignore stick drift
        return std::abs(v) < 0.18f ? 0.0f : v;
    }

    float clampAxis(float a)
    {
        return std::clamp(a, -1.0f, 1.0f);
    }

    int firstGamepad()
    {
        for (int joystick = GLFW_JOYSTICK_1; joystick <= GLFW_JOYSTICK_LAST; ++joystick)
        {
            if (glfwJoystickIsGamepad(joystick) == GLFW_TRUE)
            {
                return joystick;
            }
        }

        return -1;
    }

    // return InputSystem from window
    InputSystem* fromWindow(GLFWwindow* window)
    {
        return static_cast<InputSystem*>(glfwGetWindowUserPointer(window));
    }
}

// start listening for input
// connects inputsystem to the window
void InputSystem::attach(GLFWwindow* window)
{
    if (!window)
    {
        return;
    }

    // Clear old input and disable mouse look
    state_.clear();
    mouseLooking_ = false;
    // Store the current InputSystem object's address inside the window
    glfwSetWindowUserPointer(window, this);
    // set callbacks
    glfwSetKeyCallback(window, onKey);
    glfwSetMouseButtonCallback(window, onMouseButton);
    glfwSetCursorPosCallback(window, onCursorPosition);
    glfwSetWindowFocusCallback(window, onFocus);
}

// stop listening
void InputSystem::detach(GLFWwindow* window)
{
    if (!window)
    {
        return;
    }

    // release mouse
    // clear callbacks
    // remove inputsystem pointer
    // clear all saved input
    releaseMouse(window);
    glfwSetKeyCallback(window, nullptr);
    glfwSetMouseButtonCallback(window, nullptr);
    glfwSetCursorPosCallback(window, nullptr);
    glfwSetWindowFocusCallback(window, nullptr);
    glfwSetWindowUserPointer(window, nullptr);
    state_.clear();
}

// glfw calls this whenever any keyboard event happens
// args:
// window - window that received the event
// key - which key
// unnamed int - scancode, not used. Apparently we can just not name it & ignore it lol. Glfw will still try to pass it, but we dont need it.
// action - press, release or repeat
// mods - ctrl, shift, etc
void InputSystem::onKey(GLFWwindow* window, int key, int, int action, int mods)
{
    InputSystem* self = fromWindow(window);

    if (!self)
    {
        return;
    }

    // if its pressed, then its down
    // if its released, then its up
    if (action == GLFW_PRESS)
    {
        self->state_.keyDown(key, mods);
    }
    else if (action == GLFW_RELEASE)
    {
        self->state_.keyUp(key);
    }
}

// same but mouse
void InputSystem::onMouseButton(GLFWwindow* window, int button, int action, int)
{
    InputSystem* self = fromWindow(window);

    if (!self)
    {
        return;
    }

    if (action == GLFW_PRESS)
    {
        self->state_.mouseDown(button);
    }
    else if (action == GLFW_RELEASE)
    {
        self->state_.mouseUp(button);
    }
}

// Whenever the mouse moves, tell the input state its new position
void InputSystem::onCursorPosition(GLFWwindow* window, double x, double y)
{
    InputSystem* self = fromWindow(window);

    if (self)
    {
        self->state_.cursorMoved(x, y);
    }
}

// If the window still has focus, do nothing.
// otherwise clear inputs & release mouse
void InputSystem::onFocus(GLFWwindow* window, int focused)
{
    InputSystem* self = fromWindow(window);

    if (!self || focused == GLFW_TRUE)
    {
        return;
    }

    // Avoid stuck WASD/RMB if lost focus mid-press
    self->state_.clear();
    self->releaseMouse(window);
}

bool InputSystem::hasGamepad() const
{
    return firstGamepad() >= 0;
}

void InputSystem::releaseMouse(GLFWwindow* window)
{
    if (!window)
    {
        return;
    }

    // if mouse captured, make cursosr normal
    if (mouseLooking_)
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    // Stop mouse look and reset stored cursor movement, preventing camera jumps
    mouseLooking_ = false;
    state_.resetCursor();
}

// HO LY
// read current input & build movement commands for players.
// args:
// window - window.
// dt - delta time.
// multiplayer - if player 2 is enabled.
// uiKeybaord - if ui wants keyboard input (usually no).
// uiMouse - if ui wnats mouse input (more often yes than not).
// allowGamepad - if controller input is allowed.
FrameCommands InputSystem::buildCommands(GLFWwindow* window, float dt, bool multiplayer,
                                         bool uiKeyboard, bool uiMouse, bool allowGamepad)
{
    // will contain player movement & camera inputs
    FrameCommands commands;

    // Deliver one-time action events (F1/F2/F3/F4/Esc) via subscriptions.
    // Continuous movement still reads the held key state further below.
    //
    // Checks input events and runs subscribed action callbacks.
    // For example, pressing F4 might toggle splitscreen.
    actions_.dispatch(state_, uiKeyboard);

    // RMB starts camera look only outside imgui. Continue until the button is released.
    // reqs for camera look - rmb held && mouse already looking or ui not using mouse.
    const bool wantsLook = state_.mouseHeld(GLFW_MOUSE_BUTTON_RIGHT) && (mouseLooking_ || !uiMouse);

    // If the desired mouse look state is different from the current one, update it.
    if (wantsLook != mouseLooking_)
    {
        // GLFW_CURSOR_DISABLED - Capture/hide the mouse for camera movement.
        // GLFW_CURSOR_NORMAL - Restore the normal cursor.
        mouseLooking_ = wantsLook;
        glfwSetInputMode(window, GLFW_CURSOR, mouseLooking_ ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        // Ignore cursor jumps caused by changing mouse modes.
        state_.resetCursor();
    }

    // Move the camera based on how far the mouse moved.
    // mouseDeltaX - Horizontal mouse movement.
    // mouseDeltaY - Vertical mouse movement.
    // lookYaw - Look left/right.
    // lookPitch - Look up/down.
    // 0.13f - Mouse sensitivity.
    if (mouseLooking_)
    {
        commands.player[0].lookYaw = static_cast<float>(state_.mouseDeltaX()) * 0.13f;
        commands.player[0].lookPitch = -static_cast<float>(state_.mouseDeltaY()) * 0.13f;
    }

    // only process if ui is not using keyboard
    if (!uiKeyboard)
    {
        // player 1 command object
        PlayerCommand& p1 = commands.player[0];
        // Hideo Kojima levels of genius
        // state_.held returns true or false
        // BUT you can convert it to float to get 1.0 (true) or 0.0 (false)
        // so that means holding W is 1-0, so +1
        // holding S is 0-1, so -1
        // neither is 0-0 so 0
        // both is 1-1 so 0
        // same with other directions
        p1.forward = float(state_.held(GLFW_KEY_W)) - float(state_.held(GLFW_KEY_S));
        p1.right = float(state_.held(GLFW_KEY_D)) - float(state_.held(GLFW_KEY_A));
        p1.up = float(state_.held(GLFW_KEY_SPACE)) - float(state_.held(GLFW_KEY_LEFT_CONTROL));
        p1.sprint = state_.held(GLFW_KEY_LEFT_SHIFT);

        // if player 2 is enabled, do same for player 2
        if (multiplayer)
        {
            PlayerCommand& p2 = commands.player[1];
            p2.forward = float(state_.held(GLFW_KEY_I)) - float(state_.held(GLFW_KEY_K));
            p2.right = float(state_.held(GLFW_KEY_L)) - float(state_.held(GLFW_KEY_J));
            p2.up = float(state_.held(GLFW_KEY_U)) - float(state_.held(GLFW_KEY_O));
            // 105.0f is rotation speed
            p2.lookYaw = (float(state_.held(GLFW_KEY_RIGHT)) - float(state_.held(GLFW_KEY_LEFT))) * 105.0f * dt;
            p2.lookPitch = (float(state_.held(GLFW_KEY_UP)) - float(state_.held(GLFW_KEY_DOWN))) * 105.0f * dt;
            p2.sprint = state_.held(GLFW_KEY_RIGHT_SHIFT);
        }
    }

    // GLFW has no event callback for gamepad axes. Keep the existing controller polling.
    // if multiplayer & allowgamepade are enabled - get first available, otherwise use -1
    const int joystick = multiplayer && allowGamepad ? firstGamepad() : -1;

    // only if found controller
    if (joystick >= 0)
    {
        // this will hold controller input
        GLFWgamepadstate gamepad{};
        // use glfw to fill it with current controller inputs
        if (glfwGetGamepadState(joystick, &gamepad) == GLFW_TRUE)
        {
            PlayerCommand& p2 = commands.player[1];
            // left stick vertical movement is forward/backward
            // left stick horizontal is left/right
            p2.forward = clampAxis(p2.forward - deadzone(gamepad.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]));
            p2.right = clampAxis(p2.right + deadzone(gamepad.axes[GLFW_GAMEPAD_AXIS_LEFT_X]));
            // right stick is kinda same, except we add the values instead of replacing
            // 125.0f is sensitivity
            p2.lookYaw += deadzone(gamepad.axes[GLFW_GAMEPAD_AXIS_RIGHT_X]) * 125.0f * dt;
            p2.lookPitch -= deadzone(gamepad.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]) * 125.0f * dt;
            // press A to go up
            // B to go down
            // both/neither - nothing
            p2.up = clampAxis(p2.up + float(gamepad.buttons[GLFW_GAMEPAD_BUTTON_A] == GLFW_PRESS)
                                    - float(gamepad.buttons[GLFW_GAMEPAD_BUTTON_B] == GLFW_PRESS));
            // sprint if either keyboard spring held or left bumper held
            p2.sprint = p2.sprint || gamepad.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] == GLFW_PRESS;
        }
    }

    // finish frame
    state_.endFrame();
    // return finished commands to the game
    return commands;
}
