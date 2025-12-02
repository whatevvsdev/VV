#include <string>

#include "engine/renderer/renderer.h"
#include "game/game.h"
#include "common/io.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui_impl_sdl3.h"

constexpr int client_area_width { 1920 };
constexpr int client_area_height { 1080 };

SDL_Window* sdl_window{ nullptr };

bool initalize_sdl()
{
    if(!SDL_Init( SDL_INIT_VIDEO))
    {
        SDL_Log( "SDL could not initialize! SDL error: %s\n", SDL_GetError() );
        return false;
    }

    sdl_window = SDL_CreateWindow( "VV", client_area_width, client_area_height, SDL_WINDOW_VULKAN);

    if(sdl_window == nullptr)
    {
        SDL_Log( "Window could not be created! SDL error: %s\n", SDL_GetError() );
        return false;
    }

    return true;
}

int main( int argc, char* args[] )
{
    if (initalize_sdl())
    {
        Renderer::initialize(sdl_window);
        Game::init(sdl_window);

        SDL_Event e;
        SDL_zero(e);

        bool quit{ false };
        while(!quit)
        {
            IO::update();
            Renderer::begin_frame();

            while(SDL_PollEvent( &e ))
            {
                ImGui_ImplSDL3_ProcessEvent(&e);

                if(e.type == SDL_EVENT_QUIT)
                    quit = true;
            }

            Game::update();
            Renderer::end_frame();
        }

        Renderer::terminate();
    }
    else
    {
        printf("Failed to initialize SDL.\n");
    }

    SDL_Quit();
    return 0;
}