#pragma once

#include "input_state.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// glfw callbacks --> input state --> input actions --> action requests

// ty Rasmus

// helps with removing the hard coding hotkeys
// kinda like, when "ToggleSplitscreen" happens, do this

// Action is a name that describes an intention
// ToggleSplitscreen or Scene1 or Quit
//
// subscriber is a function that wants to be notified when that action occurs
// there can be multiple subscribers for one function
class InputActions
{
    public:
        // any function that has no args & returns nothing is a Callback
        // instead of "std::function<void()> action;"
        // we can write Callback action;
        // example:
        // void jump()
        // {
        //     std::cout << "123";
        // }
        // Callback action = jump;
        // action(); // Prints "123"
        // Use it because we want to store functions in a vector and then call them later
        using Callback = std::function<void()>;

        // Register or rebind an action. Existing callbacks are kept if rebinded.
        // modifiers uses GLFW_MOD_* bit flags (0 = no modifier requirement).
        // args:
        // name is action name ("ToggleSplitscreen"),
        // key is keycode (GLFW_KEY_F4),
        // mods is ctrl/alt/shift flag bits,
        // global is if action is allowed while imgui is using the keyboard
        void bindKey(const std::string& name, int key, int modifiers = 0, bool global = false);

        // Subscribe functions to the key's press/release event.
        // Returns false if the named action hasn't been bound yet (if action doesn't exist or callback is empty).
        // register the action with "bindKey()" first
        bool onPress(const std::string& name, Callback callback);
        bool onRelease(const std::string& name, Callback callback);

        // Deliver queued keyboard events to subscribers, minding imgui capture.
        // Call once after glfwPollEvents() and ImGui::NewFrame().
        // args:
        // state are input events collected from glfw.
        // keyboardCaptured is if imgui wants keyboard input (for future textboxes)
        // dispatch looks at those events and runs the relevant subscribers
        void dispatch(const InputState& state, bool keyboardCaptured) const;

    private:
        // soo, for each action, we should remember which key triggers it, which mods are required, if it bypasses imgui keyboard capture & lists of press/release subscribers.
        // example:
        // actions_["ToggleSplitscreen"]:
        // key = F4
        // requiredModifiers = 0
        // global = false
        // pressed = function that requests a splitscreen toggle
        // released = function that updates lastAction text
        struct Action
        {
            int key = -1;
            int requiredModifiers = 0;
            bool global = false;
            std::vector<Callback> pressed;
            std::vector<Callback> released;
        };

        std::unordered_map<std::string, Action> actions_;
};
