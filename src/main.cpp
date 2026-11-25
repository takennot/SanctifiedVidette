#pragma once

#include "glbinding/gl/functions.h"
#include <GLFW/glfw3.h>
#include <glbinding/gl/gl.h>
#include <glbinding/glbinding.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <stb_image.h>

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <array>

#include "asset_cache.hpp"
#include "debug_ui.hpp"
#include "input.hpp"
#include "player.hpp"
#include "renderer.hpp"
#include "scene.hpp"

namespace
{
    void glfwErrorCallback(int code, const char* message)
    {
        std::cerr << "GLFW " << code << ": " << message << '\n';
    }

    void registerModels(AssetCache& assets)
    {
        assets.add("bunny", "assets/models/bunny.obj", "assets/textures/beastfur.png");
        assets.add("cat", "assets/models/cat.obj", "assets/textures/cat.png", "assets/models/cat.mtl");
        assets.add("fishclown", "assets/models/fishclown.obj", "assets/textures/clownfish.png", "assets/models/fishclown.mtl");
        assets.add("monkey", "assets/models/monkey.obj", "assets/textures/lizard_skin.png");
        assets.add("teapot", "assets/models/teapot.obj", "assets/textures/non_existent_texture.png");
    }

    void applyScene(const SceneCollection& scenes, int index, std::array<Player, 2>& players, GLFWwindow* window)
    {
        // get scene data by its index
        const SceneData& scene = scenes.at(static_cast<std::size_t>(index));
        // teleport players to start positions (taken from scene data)
        players[0].teleport(scene.player1);
        players[1].teleport(scene.player2);
        // set title to show the scene name
        glfwSetWindowTitle(window, ("Sanctified Vidette | " + scene.name).c_str());
    }

    // IMGUI MY BELOVED
    // btw this might break on high-dpi systems.
    // if that happens, use imgui display dimensions for the overlay
    void overlayPlayerLabels(int width, int height, bool multiplayer)
    {
        // lets us draw simple 2d ui primitives over the scene :D
        auto* foreground = ImGui::GetForegroundDrawList();
        // put label at screen pos. args are position, color, text
        // IM_COL32 is (r,g,b,a)
        foreground->AddText(ImVec2(12.0f, static_cast<float>(height - 29)), IM_COL32(80, 190, 255, 255), "PLAYER 1");

        if (multiplayer)
        {
            // width / 2 is for finding the dividing line between left & right player views
            foreground->AddText(ImVec2(static_cast<float>(width / 2 + 12), static_cast<float>(height - 29)), IM_COL32(255, 160, 80, 255), "PLAYER 2");
            // creates a vertical line that is 2 pixels wide
            foreground->AddLine(ImVec2(static_cast<float>(width/2),0), ImVec2(static_cast<float>(width/2), static_cast<float>(height)), IM_COL32(220,220,220,180), 2.0f);
        }
    }
}

int main()
{
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit())
    {
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // 4x msaa
    glfwWindowHint(GLFW_SAMPLES, 4);
    // 8 bit stencil buffer for shadows
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1920, 1080, "Sanctified Vidette", nullptr, nullptr);

    if (!window)
    {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return 1;
    }

    // icon
    GLFWimage icon;

    icon.pixels = stbi_load("assets/engine/icons/blackhole.png", &icon.width, &icon.height, nullptr, 4);

    if (icon.pixels)
    {
        glfwSetWindowIcon(window, 1, &icon);
        stbi_image_free(icon.pixels);
    }

    glfwMakeContextCurrent(window);
    // vsync, swapping waits for monitor refresh
    glfwSwapInterval(1);

    // glbinding provides access to OpenGL functions
    glbinding::initialize(glfwGetProcAddress);

    Renderer renderer;

    if (!renderer.initialize())
    {
        renderer.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // cache of models
    AssetCache assets;
    registerModels(assets);
    SceneCollection scenes;
    std::string sceneError;

    // maybe don't preload in the future, but eh, works for now
    if (!scenes.load({"scenes/01_creature_gallery.cyno",
                      "scenes/02_fish_parade.cyno",
                      "scenes/03_teapot_trials.cyno"},
                       sceneError))
    {
        std::cerr << sceneError << '\n';
        assets.clear();
        renderer.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // loop through all loaded scenes
    for (std::size_t s = 0; s < scenes.count(); ++s)
    {
        // for every obj in all scenes
        for (const auto& obj : scenes.at(s).objects)
        {
            // check if its in the cache
            if (!assets.find(obj.model))
            {
                // just a warning is good enough, Renderer::drawWorld() skips missing models so its okay
                std::cerr << "Warning: scene '" << scenes.at(s).name << "' references unloaded model '" << obj.model << "'\n";
            }
        }
    }

    // create imgui, then inputsystem
    // attach input callbacks, then do the imgui glfw thingy
    // so that it can chain previous callbacks
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    // Register our callbacks first: imgui installs its own and chains ours.
    InputSystem inputs;
    inputs.attach(window);
    // true to install callbacks & chain ours
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460 core");

    GameSettings settings;

    // GLFW callback -> InputState -> action -> request -> game logic.
    // Callbacks only set requests. They don't change players or render directly.
    struct ActionRequests
    {
        bool quit = false;
        bool toggleMultiplayer = false;
        int selectScene = -1;
    } requests;

    std::string lastAction = "None";

    // get existing action system from inputsystem by ref
    InputActions& actions = inputs.actions();

    // register keys as actions
    // Global, even if imgui captures keys
    actions.bindKey("Quit", GLFW_KEY_ESCAPE, 0, true);
    actions.bindKey("ToggleMultiplayer", GLFW_KEY_F4);
    actions.bindKey("Scene1", GLFW_KEY_F1);
    actions.bindKey("Scene2", GLFW_KEY_F2);
    actions.bindKey("Scene3", GLFW_KEY_F3);

    // subscribe
    // [&] captures outside variables (requests, lastAction) by ref so the listener can update values in main
    // not the best way to do it because of capture lifetime but good enough
    actions.onPress("Quit", [&]
        {
            requests.quit = true;
            lastAction = "Quit: pressed";
        });

    actions.onPress("ToggleMultiplayer", [&]
        {
            requests.toggleMultiplayer = true;
            lastAction = "ToggleMultiplayer: pressed";
        });

    actions.onRelease("ToggleMultiplayer", [&]
        {
            lastAction = "ToggleMultiplayer: released"; // Example of a release subscription
        });

    actions.onPress("Scene1", [&]
        {
            requests.selectScene = 0;
            lastAction = "Scene1: pressed";
        });

    actions.onPress("Scene2", [&]
        {
            requests.selectScene = 1;
            lastAction = "Scene2: pressed";
        });

    actions.onPress("Scene3", [&]
        {
            requests.selectScene = 2;
            lastAction = "Scene3: pressed";
        });

    // create players, teleport them to spawn pos
    // get time. wordl time is separate time for animation, advances only when animation is enabled (can go forward & backward)
    std::array<Player, 2> players;
    int sceneIndex = 0;
    applyScene(scenes, sceneIndex, players, window);
    double lastTime = glfwGetTime();
    float worldTime = 0.0f;

    while (!glfwWindowShouldClose(window))
    {
        // this is where input state gets keyboard / mouse input
        glfwPollEvents();

        const double now = glfwGetTime();
        // Clamp large frame deltas after dragging/moving/minimizing the window
        // now - lastTime is elapsed seconds, so a huge pause/drag/minimize wont make the camera jump on next frame
        const float dt = std::clamp(static_cast<float>(now - lastTime), 0.0f, 0.05f);
        lastTime = now;

        // imgui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // draw debug ui returns one frame ui requests for things like scene selection and/or resetting players
        const UiActions ui = drawDebugUI(settings, scenes, sceneIndex, assets, inputs, players, lastAction);
        // exposes capture flags indicating if ui needs keyboard/mouse
        const ImGuiIO& io = ImGui::GetIO();
        // dispatches key actions, reads held input, samples the controller & returns commands for p1 & p2
        const FrameCommands cmd = inputs.buildCommands(window, dt, settings.render.multiplayer,
                                                       io.WantCaptureKeyboard, io.WantCaptureMouse,
                                                       settings.gamepadForP2);

        // act on one frame action requests
        if (requests.quit)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        if (requests.toggleMultiplayer)
        {
            settings.render.multiplayer = !settings.render.multiplayer;
        }

        // game logic stuff - changes players, worldtime, active scene etc
        // if imgui reqested a scene, prefer that, otherwise use keyboard request
        int nextScene = ui.selectScene >= 0 ? ui.selectScene : requests.selectScene;

        // check if scene index is in range & different from current scene
        // save the new scene index
        // call applyscene
        // release mouse capture just in case
        if (nextScene >= 0 && nextScene < static_cast<int>(scenes.count()) && nextScene != sceneIndex)
        {
            sceneIndex = nextScene;
            applyScene(scenes, sceneIndex, players, window);
            inputs.releaseMouse(window);
        }

        // maybe move this after player update, otherwise they theoretically can move during the same frame if moving key was held.
        // but who cares
        if (ui.resetPlayers)
        {
            applyScene(scenes, sceneIndex, players, window);
        }

        // update p1 movement from p1 command
        players[0].update(cmd.player[0], dt);

        // p2 is updatfed only when splitscreen is enabled
        if (settings.render.multiplayer)
        {
            players[1].update(cmd.player[1], dt);
        }

        // if there are animations, advance animation clock
        if (settings.animate)
        {
            worldTime += dt * settings.animationSpeed * (settings.reverseAnimation ? -1.0f : 1.0f);
        }

        // after doing the one frame action, clear the requests so they won't happen in the next frame
        requests = {};
        // since line above resets selectScene to 0, we gotta set it to -1 here, so scene doesn't change
        requests.selectScene = -1;

        // get framebuffer pixel dimensions in case if windw resized
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);

        // inc ase if minimzied
        if (width > 0 && height > 0)
        {
            const SceneData& scene = scenes.at(static_cast<std::size_t>(sceneIndex));

            // if splitscreen - move p1 camera to left, p2 to right.
            // if not - render only p1, tho p2 camera still exists, its just not rendered nor updated
            if (settings.render.multiplayer)
            {
                const int leftWidth = width / 2;

                renderer.drawViewport(0, 0, leftWidth, height, 0, players, scene, assets, settings.render, worldTime);
                renderer.drawViewport(leftWidth, 0, width - leftWidth, height, 1, players, scene, assets, settings.render, worldTime);
            }
            else
            {
                renderer.drawViewport(0, 0, width, height, 0, players, scene, assets, settings.render, worldTime);
            }

            // draw the player text
            overlayPlayerLabels(width, height, settings.render.multiplayer);
        }

        // finish building imgui draw commands
        ImGui::Render();

        if (width > 0 && height > 0)
        {
            // restore a full window viewport before drawing imgui
            // otherwise after p2 view the ui might be draw into the wrong half
            // draw ui over the scene, then display finished frame by swapping buffers
            gl::glViewport(0, 0, width, height);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }
    }

    // Clean up
    inputs.releaseMouse(window);
    ImGui_ImplOpenGL3_Shutdown();
    // restores our callbacks before detach()
    ImGui_ImplGlfw_Shutdown();
    inputs.detach(window);
    ImGui::DestroyContext();
    assets.clear();
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
