#pragma once

#include <array>
#include <cstddef>
#include <vector>

// glfw callbacks --> input state --> input actions --> action requests

// SIngle keyboard event / change recorded by glfw callbacks
// if player presses & releases, 2 events - press & release that will play out
struct KeyEvent
{
    // glfw key code
    int key = -1;
    // GLFW_MOD_* flags from when this key was pressed
    // ctrl, shift, alt
    int modifiers = 0;
    // true = press, false = release
    bool pressed = false;
};

// input state. glfw callbacks feed this object with events
// it remembers held buttons, one frame presses, relative mouse movement
class InputState
{
    public:
        // write methods called by glfw callbacks when input happens
        void keyDown(int code, int modifiers = 0);
        void keyUp(int code);
        // button is mouse button number (lmb, rmb)
        void mouseDown(int button);
        void mouseUp(int button);
        // xy are absolute coords provided by glfw
        // measured in screen/window cursor units
        // converted to relative movement later
        void cursorMoved(double x, double y);

        // read methods
        bool held(int code) const;
        bool pressed(int code) const;
        bool mouseHeld(int button) const;
        // return read only ref
        const std::vector<KeyEvent>& keyEvents() const;
        double mouseDeltaX() const;
        double mouseDeltaY() const;

        // forget previous mouse position & zero mouse movement
        // use when capturing / releasing cursor so camera dont jump
        void resetCursor();
        // delete events & deltas that belong to only this frame
        // held keys are kept
        void endFrame();
        // delete all events when window loses focus
        // should prevent input when alt tabbed.
        void clear();

    private:
        // GLFW key codes are up to 348;
        // reserve some extra space just in case and reject invalid codes
        static constexpr int kKeyCount = 512;
        // should be enough, no?
        // corsair scimitar has 12 side buttons + 3 normal, thats 15. +1 just in case.
        static constexpr int kMouseButtonCount = 16;

        // held keys
        std::array<bool, kKeyCount> keys_{};
        // pressed keys
        std::array<bool, kKeyCount> pressed_{};
        // mod flags saved at key down
        // needed for release subscriptions
        std::array<int, kKeyCount> downModifiers_{};
        // ordered list of press/release events this frame
        std::vector<KeyEvent> keyEvents_;
        // held mouse buttons
        std::array<bool, kMouseButtonCount> mouse_{};

        // if there is a previous known cursor pos
        // needed to calculate mouse move delta
        bool cursorKnown_ = false;
        // last absolute mouse pos
        double lastX_ = 0.0;
        double lastY_ = 0.0;
        // mouse delta since last endFrame()
        double deltaX_ = 0.0;
        double deltaY_ = 0.0;
};
