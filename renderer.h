#pragma once

struct SDL_Window;



namespace Renderer
{
    void initialize(SDL_Window* sdl_window_ptr);
    void update();
    void terminate();
}
