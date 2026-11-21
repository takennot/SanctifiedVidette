#include "renderer.hpp"
#include "glbinding/gl/enum.h"

#include <glbinding/gl/gl.h>
#include <glbinding/gl/functions.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstdint>

namespace
{
    // floor height, for the shadows. Should probably be replaced with "proper" shadows later
    // shadows will work only on a flat floor
    constexpr float floorY = -0.8f;

    // make quad, compute normal, store 4 vertices, append 6 indices
    // used for player & goal
    void quad(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d)
    {
        const glm::vec3 n = glm::normalize(glm::cross(b-a, c-a));
        const auto start = static_cast<std::uint32_t>(m.vertices.size());

        m.vertices.push_back({ a.x, a.y, a.z,
                               n.x, n.y, n.z,
                               0, 0 });

        m.vertices.push_back({ b.x, b.y, b.z,
                               n.x, n.y, n.z,
                               1, 0 });

        m.vertices.push_back({ c.x, c.y, c.z,
                               n.x, n.y, n.z,
                               1, 1 });

        m.vertices.push_back({ d.x, d.y, d.z,
                               n.x, n.y, n.z,
                               0, 1 });

        m.indices.insert(m.indices.end(),
                                        {
                                            start, start+1, start+2,
                                            start, start+2, start+3
                                        });
    }

    // make box, same usage as quad (player & goal)
    MeshData boxMesh()
    {
        MeshData m;
        const float s=0.5f;

        quad(m, {-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s});
        quad(m, { s,-s,-s}, {-s,-s,-s}, {-s, s,-s}, { s, s,-s});
        quad(m, { s,-s, s}, { s,-s,-s}, { s, s,-s}, { s, s, s});
        quad(m, {-s,-s,-s}, {-s,-s, s}, {-s, s, s}, {-s, s,-s});
        quad(m, {-s, s, s}, { s, s, s}, { s, s,-s}, {-s, s,-s});
        quad(m, {-s,-s,-s}, { s,-s,-s}, { s,-s, s}, {-s,-s, s});

        return m;
    }

    // uses UV now
    MeshData makeFloor()
    {
        MeshData md;
        constexpr float s = 25.0f;

        // uv - 00, 20
        // 20, 00 to repeat texture at x and/or z
        md.vertices =
        {
            {-s, floorY,  s, 0, 1, 0, 0, 0   },
            { s, floorY,  s, 0, 1, 0, 20, 0  },
            { s, floorY, -s, 0, 1, 0, 20, 20 },
            {-s, floorY, -s, 0, 1, 0, 0, 20  }
        };

        md.indices = {0, 1, 2, 0, 2, 3};

        return md;
    }

    // helper function - make model matrix that scales a cube & places it at world pos.
    // create clean identity matrix, apply translation aka movement, apply scale.
    glm::mat4 translateScale(glm::vec3 pos, glm::vec3 scale)
    {
        return glm::scale(glm::translate(glm::mat4(1.0f), pos), scale);
    }
}

bool Renderer::initialize()
{
    program_ = createProgram("shaders/a4.vert", "shaders/a4.frag");

    if (!program_)
    {
        return false;
    }

    uniforms_ = findUniforms(program_);
    floor_ = uploadMesh(makeFloor());
    cube_ = uploadMesh(boxMesh());
    floorTexture_ = loadTexture("assets/textures/trak_tile_y.jpg");

    return floor_.vao != 0 && cube_.vao != 0;
}

void Renderer::drawWorld(const SceneData& scene, const AssetCache& assets,
                         const RenderOptions& options, float worldTime, bool shadow)
{
    // go through all objects in the scene
    for (const SceneObject& instance : scene.objects)
    {
        // ask for cached/already-loaded model
        const ModelAsset* model = assets.find(instance.model);

        // if not found / registered, skip that object
        if (!model)
        {
            continue;
        }

        // pick which mesh version - tiny or custom
        const Mesh* mesh = options.customLoader ? &model->custom : &model->external;

        // if selected version doesn't exist, try the other one
        if (!mesh->vao)
        {
            mesh = options.customLoader ? &model->external : &model->custom;
        }

        // if somehow neither exist, skip
        if (!mesh->vao)
        {
            continue;
        }

        // convert xyz to vec3
        const glm::vec3 pos(instance.position.x, instance.position.y, instance.position.z);
        // get angle, yaw + total spin into radians
        const float angle = glm::radians(instance.yawDegrees + instance.rotationSpeed * worldTime);

        // make the model transformation for current object
        const glm::mat4 matrix = glm::scale(
                                            glm::rotate(glm::translate(glm::mat4(1.0f), pos), angle, glm::vec3(0,1,0)),
                                            glm::vec3(instance.scale));

        drawMesh(*mesh, uniforms_, matrix, glm::vec3(1.0f), model->texture, shadow, options.textures, options.showNormals);
    }
}

// args:
// other - player we want to see / draw.
// playerIndex - which player the avatar represents
// shadow - shadow
// options - render options
// TODO: rewrite later to use a normal mesh instead of this goofy trash
void Renderer::drawAvatar(const Player& other, int playerIndex, bool shadow,
                          const RenderOptions& options)
{
    // P1 - blue, P2 - orange
    const glm::vec3 color = playerIndex == 0 ? glm::vec3(0.26f, 0.72f, 1.0f)
                                             : glm::vec3(1.0f, 0.52f, 0.18f);

    // get eye pos / camera pos
    glm::vec3 pos = other.eyePosition;

    // make body
    // move one cube down by 0.96 units
    // stretch the cube to make a torso ish looking thingy
    // texture id is 0 because it is untextured for now
    drawMesh(cube_, uniforms_, translateScale(pos + glm::vec3(0, -0.96f, 0), {0.55f, 1.2f, 0.40f}),
             color, 0, shadow, false, options.showNormals);

    // make head
    // smaller cube slightly below eye level
    // multiply color by 0.78 to make it a lil darker
    drawMesh(cube_, uniforms_, translateScale(pos + glm::vec3(0, -0.18f, 0), {0.45f, 0.45f, 0.45f}),
             color * 0.78f, 0, shadow, false, options.showNormals);

    // A "nose" ish thingy, to show which direction player is facing
    // only this rotates with yaw
    // replace later with a head that rotates instead maybe?
    drawMesh(cube_, uniforms_, translateScale(pos + glm::vec3(0, -0.19f, 0) + other.forward() * 0.29f, {0.12f, 0.13f, 0.12f}),
             glm::vec3(0.1f), 0, shadow, false, options.showNormals);
}

void Renderer::drawGoal(const Vec3Data& goal, bool shadow, const RenderOptions& options)
{
    // goal doesn't need shadow (for now at least, maybe can have it in the future)
    if (shadow)
    {
        return;
    }

    // get the goal pos from scene data
    const glm::vec3 p(goal.x, goal.y, goal.z);

    // draw one green ish kinda thing & one yellow ish kinda thing
    // textures disabled so we can see the tint colors
    // TODO: rewrite to use a normal mesh later, maybe use shadows
    drawMesh(cube_, uniforms_, translateScale(p, {0.8f, 1.5f, 0.8f}),
            {0.2f, 1.0f, 0.38f}, 0, false, false, options.showNormals);

    drawMesh(cube_, uniforms_, translateScale(p + glm::vec3(0, 0.98f, 0), {1.1f, 0.18f, 1.1f}),
            {0.93f, 0.89f, 0.24f}, 0, false, false, options.showNormals);
}

// fml.
// args: go to .hpp
void Renderer::drawViewport(int x, int y, int width, int height, int viewerIndex,
                            const std::array<Player, 2>& players, const SceneData& scene,
                            const AssetCache& assets, const RenderOptions& options, float worldTime)
{
    // check rectangle size & ignore if impossible
    if (width <= 0 || height <= 0)
    {
        return;
    }

    // Scissor restricts rendering & glClear to this player's viewport, including depth/stencil.
    // glViewport tells opengl which "subwindow" to use. Also it controls mapping of coordinates.
    gl::glEnable(gl::GL_SCISSOR_TEST);
    gl::glScissor(x, y, width, height);
    gl::glViewport(x, y, width, height);
    gl::glClearColor(0.09f, 0.12f, 0.20f, 1.0f);
    gl::glClearStencil(0);
    gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT | gl::GL_STENCIL_BUFFER_BIT);

    // depth testing, msaa, culling, wireframe, selecting shader
    gl::glEnable(gl::GL_DEPTH_TEST);
    gl::glEnable(gl::GL_MULTISAMPLE);

    if (options.cullFaces)
    {
        gl::glEnable(gl::GL_CULL_FACE);

        // if you hate your eyes
        // gl::glCullFace(options.faceCullMode == 0 ? gl::GL_FRONT : options.faceCullMode == 2 ? gl::GL_FRONT_AND_BACK : gl::GL_BACK);

        // if you like your eyes
        switch (options.faceCullMode)
        {
            case 0:
                gl::glCullFace(gl::GL_FRONT);
                break;
            case 2:
                // why do I even have this?
                gl::glCullFace(gl::GL_FRONT_AND_BACK);
                break;
            default:
                gl::glCullFace(gl::GL_BACK);
                break;
        }
    }
    else
    {
        gl::glDisable(gl::GL_CULL_FACE);
    }

    // TERNARY IN TERNARY IN ONE LINERS WE BELIEVE
    gl::glPolygonMode(gl::GL_FRONT_AND_BACK,
                      options.polygonMode == 1 ? gl::GL_LINE :
                      options.polygonMode == 2 ? gl::GL_POINT :
                                                 gl::GL_FILL);

    gl::glUseProgram(program_);

    // choose this viewer's camera
    const glm::mat4 view = players[static_cast<std::size_t>(viewerIndex)].viewMatrix();
    // viewport aspect
    const float aspect = static_cast<float>(width) / static_cast<float>(height);

    // create projection using viewport aspect ratio
    // projection matrix
    // 70 fov
    // perspective is weird - matrix copies depth to w coord. After vertex shader, gpu divides x & y by w to get that distortion.
    // ortho is weirder - box 10.4 tall and 10.4 * aspect wide, but this time no w division so kinda mapping straight to screen?
    const glm::mat4 projection = options.perspective
                                                    ? glm::perspective(glm::radians(options.fovDegrees), aspect, 0.01f, 160.0f)
                                                    : glm::ortho(-7.0f * aspect, 7.0f * aspect, -7.0f, 7.0f, 0.01f, 160.0f);

    // upload uniforms
    gl::glUniformMatrix4fv(uniforms_.view, 1, gl::GL_FALSE, glm::value_ptr(view));
    gl::glUniformMatrix4fv(uniforms_.projection, 1, gl::GL_FALSE, glm::value_ptr(projection));
    glm::vec3 light = glm::normalize(options.lightDirection);
    gl::glUniform3f(uniforms_.lightDirection, light.x, light.y, light.z);
    gl::glUniform1i(uniforms_.albedo, 0);

    // draw ground
    drawMesh(floor_, uniforms_, glm::mat4(1.0f), glm::vec3(1), floorTexture_,
             false, options.textures, options.showNormals);

    // draw shadows
    if (options.shadows && options.polygonMode == 0)
    {
        // shadows
        // enable per pixel counter
        gl::glEnable(gl::GL_STENCIL_TEST);
        // write to all 8 bits
        gl::glStencilMask(0xFF);
        // pass only if counter is 0
        gl::glStencilFunc(gl::GL_EQUAL, 0, 0xFF);
        // If stencil fails: keep. If depth fails: keep. If both pass: add 1
        gl::glStencilOp(gl::GL_KEEP, gl::GL_KEEP, gl::GL_INCR);
        // Shadows don't write depth, so they can't block anything drawn later
        gl::glDepthMask(gl::GL_FALSE);  // Keep the floor depth.
        // for transparency
        gl::glEnable(gl::GL_BLEND);
        // shadow * alpha + floor * (1 - alpha)
        // https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBlendFunc.xhtml
        // "Transparency is best implemented using blend function (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)" - straight up yoinked from docs
        // Example with red color - if floor is 0.4 in red value and shadow is 0.02 in red - 0.02 * 0.52 (because we set alpha to 0.52 in the frag shader) + 0.4 * 0.48 (because 100-52) = 0.2024
        gl::glBlendFunc(gl::GL_SRC_ALPHA, gl::GL_ONE_MINUS_SRC_ALPHA);

        drawWorld(scene, assets, options, worldTime, true);

        // draw player 2 shadow if splitscreen
        if (options.multiplayer)
        {
            // 1 - viewerIndex
            // if camera is P1 (0), other player is P2 (1) = 1 - 0 = 1 (draw P2 body)
            // if camera is P2 (1), other player is P1 (0) = 1 - 1 = 0 (draw P1 body)
            drawAvatar(players[static_cast<std::size_t>(1 - viewerIndex)], 1 - viewerIndex, true, options);
        }

        gl::glDisable(gl::GL_BLEND);
        gl::glDisable(gl::GL_STENCIL_TEST);
        gl::glDepthMask(gl::GL_TRUE);
    }

    // draw scene objects, then goal, then players
    drawWorld(scene, assets, options, worldTime, false);
    drawGoal(scene.goal, false, options);

    if (options.multiplayer)
    {
        drawAvatar(players[static_cast<std::size_t>(1 - viewerIndex)], 1 - viewerIndex, false, options);
    }

    // unbind vao, disable wireframe so that imgui doesnt get wireframed, disable culling, disable scissor
    gl::glBindVertexArray(0);
    gl::glPolygonMode(gl::GL_FRONT_AND_BACK, gl::GL_FILL); // don't wireframe imgui
    gl::glDisable(gl::GL_CULL_FACE);
    gl::glDisable(gl::GL_SCISSOR_TEST);
}

// cleanup
void Renderer::shutdown()
{
    destroyTexture(floorTexture_);
    destroyMesh(cube_);
    destroyMesh(floor_);

    if (program_)
    {
        gl::glDeleteProgram(program_);
    }

    program_ = 0;
}
