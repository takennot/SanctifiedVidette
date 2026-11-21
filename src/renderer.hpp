// should just look at current game state and render it

#include "asset_cache.hpp"
#include "player.hpp"
#include "scene.hpp"
#include <array>

// moved render settings to a struct here.
// mostly so I can just pass 1 struct instead of 1000 separate args.
struct RenderOptions
{
    bool multiplayer = true;
    bool textures = true;
    bool showNormals = false;
    bool shadows = true;
    bool perspective = true;
    bool cullFaces = false;
    // 0 front, 1 back, 2 both
    int faceCullMode = 1;
    // 0 fill, 1 lines, 2 points
    int polygonMode = 0;
    bool customLoader = false;
    float fovDegrees = 70.0f;
    glm::vec3 lightDirection{-0.5f, 0.9f, 0.7f};
};

class Renderer
{
    public:
        // create shader program & gpu resources once
        bool initialize();
        // draw a player's view into one window rectangle
        // args:
        // xy - lower left origin of that window rectangle in framebuffer pixels.
        // width & height - size of rectangle above ^.
        // viewerIndex - 0 for P1, 1 for P2.
        // players - player states (both).
        // scene - current selected level
        // assets - preloaded objs
        // options - you're not gonna believe this, options
        // wordlTime - accumulated animation time (for rotating level objs for now)
        void drawViewport(int x, int y, int width, int height, int viewerIndex,
                          const std::array<Player, 2>& players, const SceneData& scene,
                          const AssetCache& assets, const RenderOptions& options, float worldTime);
        void shutdown();
    private:
        // draw the level's objs using the chosen cached loader mesh.
        void drawWorld(const SceneData& scene, const AssetCache& assets,
                       const RenderOptions& options, float worldTime, bool shadow);
        // (try to) draw the other player
        void drawAvatar(const Player& other, int playerIndex, bool shadow, const RenderOptions& options);
        // draw goal?... you know
        void drawGoal(const Vec3Data& goal, bool shadow, const RenderOptions& options);

        gl::GLuint program_ = 0;
        Uniforms uniforms_{};
        Mesh floor_{};
        // to draw the players & goal lmao
        // TODO: replace later with a proper player mesh
        Mesh cube_{};
        gl::GLuint floorTexture_ = 0;
};
