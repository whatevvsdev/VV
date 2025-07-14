#pragma once

struct SDL_Window;

#define RENDERER_DEBUG 1

namespace Renderer
{
    void initialize(SDL_Window* sdl_window_ptr);
    void update();
    void terminate();
}
