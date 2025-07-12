#include <string>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <Vulkan/Vulkan.h>

constexpr int client_area_width { 1280 };
constexpr int client_area_height { 720 };

SDL_Window* sdl_window{ nullptr };

bool init()
{
    if(SDL_Init( SDL_INIT_VIDEO ) == false)
    {
        SDL_Log( "SDL could not initialize! SDL error: %s\n", SDL_GetError() );
        return false;
    }

    sdl_window = SDL_CreateWindow( "VV", client_area_width, client_area_height, 0 );

    if(sdl_window == nullptr)
    {
        SDL_Log( "Window could not be created! SDL error: %s\n", SDL_GetError() );
        return false;
    }

    return true;
}

VkInstance instance;

bool init_vulkan_instance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VV";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VV";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_4;

    VkInstanceCreateInfo createInfo{};
    createInfo.pNext = nullptr;
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    createInfo.enabledLayerCount = 0;

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
    {
        printf("Failed to initialize Vulkan with vkCreateInstance.\n");
        return false;
    }
    return true;
}

int main( int argc, char* args[] )
{
    if( init() == false )
    {
        printf("Failed to initialize SDL.\n");
    }
    else
    {
        init_vulkan_instance();

        SDL_Event e;
        SDL_zero(e);

        bool quit{ false };
        while(!quit)
        {
            while(SDL_PollEvent( &e ) == true)
            {
                if(e.type == SDL_EVENT_QUIT)
                    quit = true;
            }
        }
    }

    SDL_Quit();
    return 0;
}