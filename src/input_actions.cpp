#include "input_actions.hpp"
#include <utility>

// Rebinding changes the key but does not remove subscriptions.
// So action.pressed or action.released are not cleared, which is why rebinding works.
void InputActions::bindKey(const std::string& name, int key, int modifiers, bool global)
{
    // find existing action by name, if missing - create default Action
    Action& action = actions_[name];
    action.key = key;
    action.requiredModifiers = modifiers;
    action.global = global;
}

bool InputActions::onPress(const std::string& name, Callback callback)
{
    // search without creating a new action
    auto found = actions_.find(name);

    // found == actions_.end() means not found
    // !callback means function object is empty
    if (found == actions_.end() || !callback)
    {
        return false;
    }

    // found->second is the action value in the unordered map
    // first = action name
    // second = action object
    // pressed.push_back() adds this function to the actions press-subcriber list
    // std::move(callback) transfers the callable into the vector, avoiding an unnecessary copy if possible. Does not execute the callback.
    // basically, add the callback function to actions list of functions to run when pressed
    found->second.pressed.push_back(std::move(callback));
    return true;
}

// same but released
bool InputActions::onRelease(const std::string& name, Callback callback)
{
    auto found = actions_.find(name);

    if (found == actions_.end() || !callback)
    {
        return false;
    }

    found->second.released.push_back(std::move(callback));
    return true;
}

// For loop x3 xd
//
void InputActions::dispatch(const InputState& state, bool keyboardCaptured) const
{
    // Gather callbacks before running them. A callback is then free to rebind
    // actions without changing the map while we're iterating through it.
    //
    // create empty pending vector, it will hold the callbacks we should run after checking all events.
    std::vector<Callback> pending;

    // go through all keyboard events captured this frame (for example: F4 down or F4 up)
    for (const KeyEvent& event : state.keyEvents())
    {
        // for each event, check every registered action
        // not really good, but whatever
        for (const auto& entry : actions_)
        {
            // since entry.first is action name
            // then entry.second is the Action itself
            const Action& action = entry.second;

            // if actions key is different from the event key, we skip this action
            if (action.key != event.key)
            {
                continue;
            }

            // if imgui captured the keyboard and action isn't global - skip action
            if (keyboardCaptured && !action.global)
            {
                continue;
            }

            // check mod flags, if required flags are missing - skip action
            // https://www.glfw.org/docs/latest/group__mods.html
            // fking bitwise AND thingy
            // if required is ctrl only - holding ctrl works, ctrl + shift works, shift not work
            // because ctrl + shift is ctrl included in that (0001 & 0010 = 0011)
            if ((event.modifiers & action.requiredModifiers) != action.requiredModifiers)
            {
                continue;
            }

            // if event.pressed is true, use pressed listeners, otherwise use released listeners
            const auto& listeners = event.pressed ? action.pressed : action.released;

            // add all those listeners to temp queue
            // copy all entries from start of listeners to end of listeners, inserting them at the end of pending
            pending.insert(pending.end(), listeners.begin(), listeners.end());
        }
    }

    // after collection, call each callback
    for (const Callback& callback : pending)
    {
        callback();
    }
}
