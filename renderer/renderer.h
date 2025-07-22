#pragma once

struct SDL_Window;

namespace Renderer
{
    void initialize(SDL_Window* sdl_window_ptr);
    void begin_frame();
    void end_frame();
    void terminate();
}
