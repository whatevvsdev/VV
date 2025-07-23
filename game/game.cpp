#include "game.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext.hpp> // perspective, translate, rotate

#include "../renderer/cameras.h"
#include "SDL3/SDL_keyboard.h"
#include "SDL3/SDL_mouse.h"
#include "SDL3/SDL_scancode.h"
#include "SDL3/SDL_timer.h"

#include "imgui.h"

/*
 * We are using RIGHT HANDED CARTESIAN COORDINATES
 */

struct
{
    SDL_Window* window { nullptr };

    glm::vec3 position { glm::vec3(0.0f, 0.0f, 2.0f)};
    f32 pitch {};
    f32 yaw {};
    glm::mat4 matrix {};

    glm::mat4 camera_matrix { glm::mat4(1) };

    bool locked_mouse { false };

    u64 last_frame_time_query { 0 };
} state;

glm::mat4 calculate_camera_matrix()
{
    glm::mat4 rot_matrix = glm::yawPitchRoll(glm::radians(state.yaw), glm::radians(state.pitch), 0.0f);
    glm::mat4 new_transform = glm::translate(glm::mat4(1.0f), glm::vec3(state.position)) * rot_matrix;

    return new_transform;
}

void Game::init(SDL_Window* window)
{
    SDL_SetWindowRelativeMouseMode(window, state.locked_mouse);
    SDL_SetWindowMouseGrab(window, state.locked_mouse);
    state.last_frame_time_query = SDL_GetPerformanceCounter();;
    state.window = window;
}

bool last_tabbed = false;
void Game::update()
{
    u64 current_frame_time_query { SDL_GetPerformanceCounter() };
    f32 frame_delta_ms = static_cast<f32>(static_cast<f64>(current_frame_time_query - state.last_frame_time_query) / static_cast<f64>(SDL_GetPerformanceFrequency())) * 1000.0f;
    state.last_frame_time_query = current_frame_time_query;

    // input here
    // TOOD: don't do input like this :)
    const u8* keys = reinterpret_cast<const u8*>(SDL_GetKeyboardState(nullptr));

    glm::vec3 local_move = glm::vec3(static_cast<i32>(keys[SDL_SCANCODE_D]) - static_cast<i32>(keys[SDL_SCANCODE_A]),
                            static_cast<i32>(keys[SDL_SCANCODE_SPACE]) - static_cast<i32>(keys[SDL_SCANCODE_LCTRL]),
                            -(static_cast<i32>(keys[SDL_SCANCODE_W]) - static_cast<i32>(keys[SDL_SCANCODE_S])));

    glm::vec3 global_move = state.camera_matrix * glm::vec4(local_move, 0.0f);
    state.position += global_move * frame_delta_ms * 0.001f;

    f32 mouse_dx { 0.0f };
    f32 mouse_dy { 0.0f };
    SDL_MouseButtonFlags buttons = SDL_GetRelativeMouseState(&mouse_dx, &mouse_dy);

    if (state.locked_mouse)
    {
        state.yaw += -mouse_dx * 0.1;
        state.pitch += -mouse_dy * 0.1;
        state.pitch = glm::clamp(state.pitch, -89.0f, 89.0f);
    }

    state.camera_matrix = calculate_camera_matrix();

    Renderer::Cameras::set_current_camera_matrix(state.camera_matrix);

    if (keys[SDL_SCANCODE_TAB])
    {
        if (!last_tabbed)
        {
            state.locked_mouse = !state.locked_mouse;
            SDL_SetWindowRelativeMouseMode(state.window, state.locked_mouse);
            SDL_SetWindowMouseGrab(state.window, state.locked_mouse);
        }
        last_tabbed = true;
    }
    else
    {
        last_tabbed = false;
    }

    if (state.locked_mouse)
    {
        i32 w;
        i32 h;
        SDL_GetWindowSizeInPixels(state.window, &w, &h);
        SDL_WarpMouseInWindow(state.window, w * 0.5, h * 0.5);
    }

    //ImGui::SetNextWindowSize(ImVec2(300.0f, 100.0f));
    ImGui::Begin("Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("Position %.2f %.2f %.2f ", state.position.x, state.position.y, state.position.z);
    ImGui::Text("Forward %.2f %.2f %.2f ", -state.camera_matrix[2].x, -state.camera_matrix[2].y, -state.camera_matrix[2].z);
    ImGui::End();
}
