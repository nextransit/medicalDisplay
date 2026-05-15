/**
 * @file dicom_viewer.cpp
 * @brief Simple DICOM Viewer Example
 */

#include <iostream>
#include <cstdlib>
#include <SDL2/SDL.h>
#include "dicom_reader.h"
#include "display_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

// Window dimensions
static const int WINDOW_WIDTH = 1024;
static const int WINDOW_HEIGHT = 1024;

// Global state
static SDL_Window* g_window = nullptr;
static SDL_Renderer* g_renderer = nullptr;
static SDL_Texture* g_texture = nullptr;

static void render_texture(const uint16_t* pixels, int width, int height, int stride) {
    if (!g_renderer || !pixels) return;
    
    // Create texture if needed
    if (!g_texture || SDL_QueryTexture(g_texture, nullptr, nullptr, nullptr, nullptr) != 0) {
        if (g_texture) SDL_DestroyTexture(g_texture);
        g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGB332,
                                      SDL_TEXTUREACCESS_STREAMING, width, height);
    }
    
    if (!g_texture) return;
    
    // Convert 16-bit to 8-bit for display
    uint8_t* surface_pixels = new uint8_t[width * height];
    for (int i = 0; i < width * height; i++) {
        uint16_t pixel = pixels[i];
        // Window to 0-255 range (assuming 12-bit input)
        int val = (pixel >> 4) & 0xFF;  // Take top 8 bits of 12-bit
        surface_pixels[i] = (uint8_t)val;
    }
    
    // Update texture
    SDL_UpdateTexture(g_texture, nullptr, surface_pixels, width);
    
    // Render
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);
    SDL_RenderPresent(g_renderer);
    
    delete[] surface_pixels;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <dicom_file>" << std::endl;
        return 1;
    }
    
    const char* dicom_path = argv[1];
    
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    
    // Create window
    g_window = SDL_CreateWindow("Medical DICOM Viewer",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                WINDOW_WIDTH, WINDOW_HEIGHT,
                                SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }
    
    // Create renderer
    g_renderer = SDL_CreateRenderer(g_window, -1,
                                    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return 1;
    }
    
    // Open DICOM file
    std::cout << "Opening: " << dicom_path << std::endl;
    
    DICOM_Dataset* dataset = dicom_open(dicom_path);
    if (!dataset) {
        std::cerr << "Failed to open DICOM file: " << dicom_path << std::endl;
        SDL_DestroyRenderer(g_renderer);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return 1;
    }
    
    // Extract metadata
    DICOM_Metadata metadata;
    dicom_extract_metadata(dataset, &metadata);
    
    std::cout << "Patient: " << metadata.patient_name << std::endl;
    std::cout << "Modality: " << metadata.modality << std::endl;
    std::cout << "Study: " << metadata.study_description << std::endl;
    
    // Read pixel data
    DICOM_PixelData pixel_info;
    if (dicom_read_pixels(dataset, &pixel_info) != 0) {
        std::cerr << "Failed to read pixel data" << std::endl;
        dicom_close(dataset);
        SDL_DestroyRenderer(g_renderer);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return 1;
    }
    
    std::cout << "Image size: " << pixel_info.width << "x" << pixel_info.height << std::endl;
    std::cout << "Bits stored: " << pixel_info.bits_stored << std::endl;
    
    // Update window title with patient info
    char title[256];
    snprintf(title, sizeof(title), "DICOM Viewer - %s [%s]",
             metadata.patient_name, metadata.modality);
    SDL_SetWindowTitle(g_window, title);
    
    // Main loop
    bool running = true;
    SDL_Event event;
    
    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                    
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        running = false;
                    }
                    break;
                    
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        // Handle resize if needed
                    }
                    break;
            }
        }
        
        // Render (simplified - would apply window/level)
        if (pixel_info.pixel_data) {
            render_texture((const uint16_t*)pixel_info.pixel_data,
                          pixel_info.width, pixel_info.height, pixel_info.width * 2);
        }
        
        SDL_Delay(16);  // ~60 FPS
    }
    
    // Cleanup
    if (g_texture) SDL_DestroyTexture(g_texture);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    dicom_close(dataset);
    SDL_Quit();
    
    return 0;
}
