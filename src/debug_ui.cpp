#include "debug_ui.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <vector>

UiActions drawDebugUI(GameSettings& settings, const SceneCollection& scenes,
                      int sceneIndex, const AssetCache& assets, const InputSystem& input,
                      const std::array<Player, 2>& players, const std::string& lastAction)
{
    UiActions actions;
    // default imgui pos if first use
    ImGui::SetNextWindowPos(ImVec2(14, 14), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(375, 560), ImGuiCond_FirstUseEver);
    // create settings window
    ImGui::Begin("Sanctified Vidette");
    ImGui::TextUnformatted("F4: Toggle split screen | F1/F2/F3: Scenes");
    ImGui::Text("Last keyboard action: %s", lastAction.c_str());
    ImGui::Checkbox("Splitscreen", &settings.render.multiplayer);

    // combo wants array of c style strings, so temp vector it is
    std::vector<const char*> labels;

    for (std::size_t i = 0; i < scenes.count(); ++i)
    {
        labels.push_back(scenes.at(i).name.c_str());
    }

    // set dropdown to currently selected scene
    int requested = sceneIndex;

    // returns true if dropdown is changed
    // labels.data() is pointer to first element of array
    if (ImGui::Combo("Scene", &requested, labels.data(), static_cast<int>(labels.size())))
    {
        // return the new selected index as uiactions.selectscene
        actions.selectScene = requested;
    }

    // returns true on click
    if (ImGui::Button("Reset both players to scene starts"))
    {
        // set a request, main will teleport later
        actions.resetPlayers = true;
    }

    ImGui::Separator();

    ImGui::TextUnformatted("Player 1: WASD / Space / LCtrl / Shift");
    ImGui::TextUnformatted("Hold right mouse button to look around");
    ImGui::TextUnformatted("Player 2: IJKL / U,O / arrow keys / RShift");
    ImGui::Checkbox("Enable gamepad for Player 2", &settings.gamepadForP2);
    // searches for glfw mapped gamepads
    ImGui::Text("Gamepad: %s", input.hasGamepad() ? "detected (first mapped controller)" : "not detected (keyboard works)");
    ImGui::TextUnformatted("ESC: Quit");
    ImGui::Separator();

    int loader = settings.render.customLoader ? 1 : 0;
    ImGui::RadioButton("TinyObjLoader", &loader, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Custom OBJ", &loader, 1);
    settings.render.customLoader = loader == 1;
    ImGui::Checkbox("Textures", &settings.render.textures);
    ImGui::Checkbox("Visualize normals", &settings.render.showNormals);
    ImGui::Checkbox("Projected shadows", &settings.render.shadows);
    ImGui::Checkbox("Perspective", &settings.render.perspective);
    ImGui::Checkbox("Face culling", &settings.render.cullFaces);
    ImGui::RadioButton("Cull front", &settings.render.faceCullMode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Cull back", &settings.render.faceCullMode, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Cull both", &settings.render.faceCullMode, 2);
    ImGui::RadioButton("Solid", &settings.render.polygonMode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Wireframe", &settings.render.polygonMode, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Points", &settings.render.polygonMode, 2);
    ImGui::SliderFloat("FOV", &settings.render.fovDegrees, 10.0f, 110.0f);

    float light[3] = {
                        settings.render.lightDirection.x,
                        settings.render.lightDirection.y,
                        settings.render.lightDirection.z
                     };

    if (ImGui::SliderFloat3("Light direction", light, -1.0f, 1.0f))
    {
        // Projected shadows require the light to come from above the floor
        settings.render.lightDirection = {
                                            light[0],
                                            light[1] < 0.1f ? 0.1f : light[1],
                                            light[2]
                                         };
    }

    ImGui::Checkbox("Animate models", &settings.animate);
    ImGui::Checkbox("Reverse rotation", &settings.reverseAnimation);
    ImGui::SliderFloat("Animation speed", &settings.animationSpeed, 0.1f, 100.0f);
    ImGui::Separator();

    // stats
    ImGui::Text("Registered models: %zu", assets.uniqueModels());
    ImGui::Text("OBJ file reads: %d", assets.diskReads());
    ImGui::Text("GPU mesh uploads: %d", assets.gpuUploads());
    ImGui::Text("Scene objects: %zu", scenes.at(static_cast<std::size_t>(sceneIndex)).objects.size());

    // get goal location from current scene, conver to glm::vec3
    const Vec3Data& goal = scenes.at(static_cast<std::size_t>(sceneIndex)).goal;
    const glm::vec3 goalPos(goal.x, goal.y, goal.z);

    // loop through 1 or 2 players (depending on if it is multiplayer or not)
    // lowkey maybe can just get players.size here instead? input.gpp --> FrameCommands struct --> players array
    for (int i = 0; i < (settings.render.multiplayer ? 2 : 1); ++i)
    {
        // get vector from goal to player (ty Sergei for reminding me vector math 💀)
        // glm::length shooould get straight line distance
        const float distance = glm::length(players[static_cast<std::size_t>(i)].eyePosition - goalPos);

        // round it to 1 decimal
        // if distance is less than 2, show goal reached
        ImGui::Text("P%d goal distance: %.1f %s", i + 1, distance, distance < 2.0f ? "(GOAL REACHED)" : "");
    }

    if (ImGui::CollapsingHeader("Cached OBJ triangle counts"))
    {
        // go through cache stats
        for (const auto& pair : assets.items())
        {
            // first is model name
            // second is ModelAsset
            ImGui::BulletText("%s: custom %zu, tiny %zu", pair.first.c_str(), pair.second.customTriangles, pair.second.externalTriangles);
        }
    }

    ImGui::End();
    return actions;
}
