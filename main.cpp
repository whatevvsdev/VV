#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <string>

constexpr int client_area_width { 1280 };
constexpr int client_area_height { 720 };

SDL_Window* sdl_window{ nullptr };

bool init()
{
    bool success{ true };

    if( SDL_Init( SDL_INIT_VIDEO ) == false )
    {
        SDL_Log( "SDL could not initialize! SDL error: %s\n", SDL_GetError() );
        success = false;
    }
    else
    {
        if( sdl_window = SDL_CreateWindow( "VV", client_area_width, client_area_height, 0 ); sdl_window == nullptr )
        {
            SDL_Log( "Window could not be created! SDL error: %s\n", SDL_GetError() );
            success = false;
        }
    }

    return success;
}


int main( int argc, char* args[] )
{
    if( init() == false )
    {
        SDL_Log( "Unable to initialize program!\n" );
    }
    else
    {
        //The quit flag
        bool quit{ false };

        //The event data
        SDL_Event e;
        SDL_zero( e );

        //The main loop
        while( quit == false )
        {
            //Get event data
            while( SDL_PollEvent( &e ) == true )
            {
                //If event is quit type
                if( e.type == SDL_EVENT_QUIT )
                {
                    //End the main loop
                    quit = true;
                }
            }

            //Update the surface
            SDL_UpdateWindowSurface( sdl_window );
        }
    }

    SDL_Quit();

    return 0;
}
