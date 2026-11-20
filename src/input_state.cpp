#include "input_state.hpp"

// btw, have to keep casting to size_t because got warnings without it

void InputState::keyDown(int code, int modifiers)
{
    // check if valid keycode
    if (code < 0 || code >= kKeyCount)
    {
        return;
    }

    // if key was not already held
    // only first down event should count as new press
    if (!keys_[static_cast<std::size_t>(code)])
    {
        // it was pressed during this frame
        pressed_[static_cast<std::size_t>(code)] = true;
        // save mods
        downModifiers_[static_cast<std::size_t>(code)] = modifiers;
        // add to events list
        keyEvents_.push_back({code, modifiers, true});
    }

    // set held to true even if key was already held
    // example if keyDown(F4) runs 3 times while F4 is down, then keys_[F4] is true
    // but the event list gets only one new press event.
    // this should prevent F4 from toggling splitscreen on every frame lol
    keys_[static_cast<std::size_t>(code)] = true;
}

// same same but different
void InputState::keyUp(int code)
{
    if (code < 0 || code >= kKeyCount)
    {
        return;
    }

    // queue a release if it was held
    if (keys_[static_cast<std::size_t>(code)])
    {
        // use mods from press
        // if release ctrl before S, it should still do the ctrl+s release
        keyEvents_.push_back({code, downModifiers_[static_cast<std::size_t>(code)], false});
    }

    // set held to false because we literally just released it
    keys_[static_cast<std::size_t>(code)] = false;
}

// same as keyboard tbh, just no queue
void InputState::mouseDown(int button)
{
    if (button < 0 || button >= kMouseButtonCount)
    {
        return;
    }

    mouse_[static_cast<std::size_t>(button)] = true;
}

void InputState::mouseUp(int button)
{
    if (button < 0 || button >= kMouseButtonCount)
    {
        return;
    }

    mouse_[static_cast<std::size_t>(button)] = false;
}

// since glfw reports current absolute pos
// if we know last pos, we can get delta by
// current - previous
// add that delta to frame's total. (in case if there are multiple cursor events before frame is rendered)
void InputState::cursorMoved(double x, double y)
{
    if (cursorKnown_)
    {
        deltaX_ += x - lastX_;
        deltaY_ += y - lastY_;
    }

    lastX_ = x;
    lastY_ = y;
    cursorKnown_ = true;
}

// diabolical getters
// is key currently down
// check if valid, then if held
bool InputState::held(int code) const
{
    return code >= 0 && code < kKeyCount && keys_[static_cast<std::size_t>(code)];
}

// same but press
bool InputState::pressed(int code) const
{
    return code >= 0 && code < kKeyCount && pressed_[static_cast<std::size_t>(code)];
}

const std::vector<KeyEvent>& InputState::keyEvents() const
{
    return keyEvents_;
}

// same but mouse buttons
bool InputState::mouseHeld(int button) const
{
    return button >= 0 && button < kMouseButtonCount && mouse_[static_cast<std::size_t>(button)];
}

double InputState::mouseDeltaX() const
{
    return deltaX_;
}

double InputState::mouseDeltaY() const
{
    return deltaY_;
}

void InputState::resetCursor()
{
    cursorKnown_ = false;
    deltaX_ = 0.0;
    deltaY_ = 0.0;
}

void InputState::endFrame()
{
    // fill should set every key's "pressed this frame" flag to false
    pressed_.fill(false);
    keyEvents_.clear();
    deltaX_ = 0.0;
    deltaY_ = 0.0;
}

void InputState::clear()
{
    keys_.fill(false);
    pressed_.fill(false);
    downModifiers_.fill(0);
    keyEvents_.clear();
    mouse_.fill(false);
    resetCursor();
}
