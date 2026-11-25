#pragma once

constexpr int THREADS_PER_BLOCK = 256;
constexpr bool FACE_TRANSITION = true;
constexpr bool CUBE_TRANSITION = false;

constexpr float CAMERA_Z_NEAR = 0.1F;
constexpr float CAMERA_Z_FAR = 1000.0F;
constexpr float CAMERA_FOV_DEG = 60.0F;

constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 600;

constexpr float WORLD_TICK_RATE = 1.0F;
constexpr float RENDER_TICK_RATE = 60.0F;
constexpr float INPUT_TICK_RATE = 10.0F;
constexpr double PLAYER_SPEED = 1.0;
constexpr double MOUSE_SENSITIVITY = 0.002;

