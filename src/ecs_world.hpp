#pragma once
#include "scene.hpp"
#include "player.hpp"
#include <flecs.h>
#include <array>
#include <vector>
#include <stdexcept>
#include <utility>

// The world owns live components. Only cached queries.
class GameWorld
{
    public:
        GameWorld();
        void activate(const SceneData& scene);
        void resetPlayers(const SceneData& scene);
        void update(const FrameCommands& commands, bool multiplayer, float dt, float animationFactor);
        CameraPose camera(int index) const;
        Position goalPosition() const;

        std::size_t entityCount() const
        {
            return entities_.size();
        }

        template<class F> void eachRenderable(F&& f) const
        {
            renderables_.each(std::forward<F>(f));
        }

        template<class F> void eachNamed(F&& f) const
        {
            names_.each(std::forward<F>(f));
        }

    private:
        flecs::world world_;
        std::vector<flecs::entity> entities_; // handles for scene lifetime
        flecs::query<Position, Orientation, const Player> players_;
        flecs::query<const Position, const Orientation, const Player> cameras_;
        flecs::query<Orientation, const Rotation> rotating_;
        flecs::query<const Position, const Orientation, const Scale, const Mesh, const Texture> renderables_;
        flecs::query<const Position> goals_;
        flecs::query<const Name> names_;
};
