#pragma once
#include <string>

struct Mesh
{
    // asset cache key
    std::string model;
};

struct Texture
{
    std::string path = "default";
};

struct Position
{
    float x = 0, y = 0, z = 0;
};

struct Player
{
    // 0 = P1
    // 1 = P2
    int index = 0;
};

struct Name
{
    std::string value;
};

struct Scale
{
  float value = 1;
};

struct Orientation
{
    // degrees
    float yaw = 0, pitch = 0;
};

struct Rotation
{
    float degreesPerSecond = 0;
};

// Flecs tag, just add this to mark something as a goal
struct Goal
{

};
