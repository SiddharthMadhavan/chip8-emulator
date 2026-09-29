#include "chip8.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#include <SDL2/SDL_error.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_rect.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>
#include <cstdint>
#include <iostream>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <string>
#include <filesystem>
#include <cmath>

const int SCALE = 10; // Each pixel is 10x10 screen pixels
const int WIDTH = 128*SCALE;
const int HEIGHT = 64*SCALE;

enum class AppState {
    MAIN_MENU,
    ROM_SELECT,
    PLAYING
};

enum class Waveform { 
    SQUARE, 
    SINE, 
    TRIANGLE, 
    SAWTOOTH, 
    COUNT // Used to wrap around the options
};

struct AudioData {
    bool beeping;
    Waveform current_wave;
};

// Keyboard mapping
uint8_t keymap[16] = {
    SDLK_x, // 0
    SDLK_1, // 1
    SDLK_2, // 2
    SDLK_3, // 3
    SDLK_q, // 4
    SDLK_w, // 5
    SDLK_e, // 6
    SDLK_a, // 7
    SDLK_s, // 8
    SDLK_d, // 9
    SDLK_z, // A
    SDLK_c, // B
    SDLK_4, // C
    SDLK_r, // D
    SDLK_f, // E
    SDLK_v  // F
};

struct ColourTheme{
    SDL_Color bg;
    SDL_Color fg;
};

const std::vector<ColourTheme> themes = {
    {{0, 0, 0 , 255}, {255, 255, 255, 255}},
    {{0, 20, 0 , 255}, {50, 255, 50, 255}},
    {{40, 15, 0 , 255}, {255, 170, 0, 255}},
    {{15, 56, 15 , 255}, {155, 188, 15, 255}}
};

std::vector<std::string> paused_options = {
    "Load",
    "Save",
    "Change Speed Mode",
    "Change Theme",
    "Change Waveform",
    "Main Menu"
};

std::vector<std::string> get_roms(){
    std::vector<std::string> roms;

    if(!std::filesystem::exists("roms"))
        return roms;

    for(const auto& entry :
        std::filesystem::directory_iterator("roms")){

        if(entry.path().extension() == ".ch8"){
            roms.push_back(entry.path().string());
        }
    }

    return roms;
}

void audio_callback(void* userdata, uint8_t* stream, int len){
    static uint32_t sample_index = 0;
    int16_t* audio_buffer = (int16_t*) stream;
    int samples = len / 2;

    AudioData* audio_data = (AudioData*) userdata;
    const double PI = 3.14159265358979323846;
    
    for(int i = 0; i < samples; i++){
        if(audio_data->beeping){
            double time = (double)sample_index / 44100.0;
            double freq = 440.0; // 440Hz pitch
            double angular_freq = 2.0 * PI * freq * time;
            int16_t value = 0;
            
            switch(audio_data->current_wave) {
                case Waveform::SQUARE:
                    value = std::sin(angular_freq) > 0 ? 3000 : -3000;
                    break;
                case Waveform::SINE:
                    value = 3000 * std::sin(angular_freq);
                    break;
                case Waveform::TRIANGLE:
                    value = 3000 * (2.0 / PI) * std::asin(std::sin(angular_freq));
                    break;
                case Waveform::SAWTOOTH:
                    value = 3000 * (2.0 * std::fmod(time * freq, 1.0) - 1.0);
                    break;
                default:
                    value = 0;
            }
            audio_buffer[i] = value;
            sample_index++;
        }
        else {
            audio_buffer[i] = 0; // Silence
            sample_index = 0;
        }
    }
}

void draw_text(
    SDL_Renderer* renderer,
    TTF_Font* font,
    const std::string& text,
    int x,
    int y,
    SDL_Color colour
){
    SDL_Surface* surface =
        TTF_RenderText_Solid(font, text.c_str(), colour);

    if(!surface)
        return;

    SDL_Texture* texture =
        SDL_CreateTextureFromSurface(renderer, surface);

    if(texture){
        SDL_Rect dest = {
            x,
            y,
            surface->w,
            surface->h
        };

        SDL_RenderCopy(renderer, texture, NULL, &dest);
        SDL_DestroyTexture(texture);
    }

    SDL_FreeSurface(surface);
}

std::string main_menu(
    SDL_Renderer* renderer,
    TTF_Font* font,
    bool& running
){
    std::vector<std::string> roms = get_roms();

    int selected = 0;

    bool menu_running = true;

    while(menu_running && running){

        SDL_Event event;

        while(SDL_PollEvent(&event)){

            if(event.type == SDL_QUIT){
                running = false;
                return "";
            }

            if(event.type == SDL_KEYDOWN){

                if(event.key.keysym.sym == SDLK_UP ||
                   event.key.keysym.sym == SDLK_w){

                    selected--;

                    if(selected < 0)
                        selected = roms.size();
                }

                else if(event.key.keysym.sym == SDLK_DOWN ||
                        event.key.keysym.sym == SDLK_s){

                    selected++;

                    if(selected > (int)roms.size())
                        selected = 0;
                }

                else if(event.key.keysym.sym == SDLK_RETURN){

                    if(selected == (int)roms.size()){
                        running = false;
                        return "";
                    }

                    return roms[selected];
                }

                else if(event.key.keysym.sym == SDLK_ESCAPE){

                    running = false;
                    return "";
                }
            }
        }
                SDL_SetRenderDrawColor(
            renderer,
            0, 0, 0, 255
        );

        SDL_RenderClear(renderer);

        SDL_Color white = {
            255, 255, 255, 255
        };

        SDL_Color yellow = {
            255, 255, 0, 255
        };


        draw_text(
            renderer,
            font,
            "CHIP-8 EMULATOR",
            450,
            30,
            white
        );


        for(int i = 0; i < (int)roms.size(); i++){

            std::filesystem::path path(roms[i]);

            draw_text(
                renderer,
                font,
                path.stem().string(),
                200,
                90 + i * 70,
                i == selected
                    ? yellow
                    : white
            );
        }


        draw_text(
            renderer,
            font,
            "Exit",
            240,
            90 + roms.size() * 70,
            selected == (int)roms.size()
                ? yellow
                : white
        );


        SDL_RenderPresent(renderer);

        SDL_Delay(16);
    }

    return "";
}

void draw_graphics(SDL_Renderer* renderer, Chip8& chip8, const ColourTheme& theme){
    // Clear screen
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, theme.bg.r, theme.bg.g, theme.bg.b, 255);
    SDL_RenderClear(renderer);
    // Drawing white pixels
    SDL_SetRenderDrawColor(renderer, theme.fg.r, theme.fg.g, theme.fg.b, 255);
    int screen_width = chip8.high_res ? 128 : 64;
    int screen_height = chip8.high_res ? 64 : 32;
    int current_scale = chip8.high_res ? SCALE : SCALE * 2; // Doubled scale for 64x32 to fill window

    for(int y = 0; y < screen_height; y++){
        for(int x = 0; x < screen_width; x++){
            if(chip8.display[x + (y * screen_width)] == 1){
                SDL_Rect rect = {x * current_scale, y * current_scale, current_scale, current_scale};
                SDL_RenderFillRect(renderer, &rect);
            }
        }
    }
    SDL_RenderPresent(renderer);
}

void draw_paused_graphics(SDL_Renderer* renderer, TTF_Font* font, int selected, int scroll, Chip8& chip8, const ColourTheme& theme) {
    // Redraw the underlying game frame first
    SDL_SetRenderDrawColor(renderer, theme.bg.r, theme.bg.g, theme.bg.b, 255);
    SDL_RenderClear(renderer);
    
    SDL_SetRenderDrawColor(renderer, theme.fg.r, theme.fg.g, theme.fg.b, 255);
    int screen_width = chip8.high_res ? 128 : 64;
    int screen_height = chip8.high_res ? 64 : 32;
    int current_scale = chip8.high_res ? SCALE : SCALE * 2; // Doubled scale for 64x32 to fill window

    for(int y = 0; y < screen_height; y++){
        for(int x = 0; x < screen_width; x++){
            if(chip8.display[x + (y * screen_width)] == 1){
                SDL_Rect rect = {x * current_scale, y * current_scale, current_scale, current_scale};
                SDL_RenderFillRect(renderer, &rect);
            }
        }
    }

    // Draw the dark semi-transparent layer
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 220);
    SDL_Rect full_screen = {0, 0, WIDTH, HEIGHT};
    SDL_RenderFillRect(renderer, &full_screen);

    if (font) {
        SDL_Color font_colour = {theme.fg.r, theme.fg.g, theme.fg.b, 255};
        SDL_Color yellow = {255, 255, 0, 255}; 

        int max_items = std::min(5, (int)paused_options.size() - scroll);

        for(int i = 0; i < max_items; i++) {
            int actual_index = scroll + i;
            SDL_Color text_color = (actual_index == selected) ? yellow : font_colour;
            
            SDL_Surface* surface = TTF_RenderText_Solid(font, paused_options[actual_index].c_str(), text_color);
            if (surface) {
                SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
                SDL_Rect dest = { 50, 50 + (i * 70), surface->w, surface->h }; 
                SDL_RenderCopy(renderer, texture, NULL, &dest);
                SDL_FreeSurface(surface);
                SDL_DestroyTexture(texture);
            }
        }
    }

    SDL_RenderPresent(renderer);
}

void handle_input(Chip8& chip8,bool& running,bool& game_running,bool& paused,int& selected,int& scroll, const std::string& save_file, int& current_theme, int& speed_level, AudioData& audio_data){
    SDL_Event event;
    int max_options = paused_options.size();

    while(SDL_PollEvent(&event)){
        if(event.type == SDL_QUIT) {
            running = false;
            game_running = false;
        }
        if(event.type == SDL_KEYDOWN){
            if(event.key.keysym.sym == SDLK_ESCAPE){
                game_running = false;
            }
            if(event.key.keysym.sym == SDLK_SPACE) {
                paused = !paused;
                selected = 0;
                scroll = 0;
                if (!paused) chip8.draw_flag = true;
            } 
            // Check which Chip-8 key was pressed
            if(paused) {
                if(event.key.keysym.sym == SDLK_w || event.key.keysym.sym == SDLK_UP) {
                    if(selected > 0) {
                        selected--;
                        if(selected < scroll) scroll = selected;
                    }
                }
                else if(event.key.keysym.sym == SDLK_s || event.key.keysym.sym == SDLK_DOWN) {
                    if(selected < max_options - 1) {
                        selected++;
                        if(selected >= scroll + 5) scroll = selected - 4;
                    }
                }
                else if(event.key.keysym.sym == SDLK_a || event.key.keysym.sym == SDLK_RETURN) {
                    switch(selected) {
                        case 0:
                            chip8.load_state(save_file);
                            paused = false;
                            selected = 0;
                            scroll = 0;
                            chip8.draw_flag = true;
                            break;
                        case 1:
                            chip8.save_state(save_file);
                            break;
                        case 2:
                            speed_level = (speed_level + 1) % 3;
                            break;
                        case 3:
                            current_theme = (current_theme + 1) % themes.size();
                            chip8.draw_flag = true;
                            break;
                        case 4:
                            audio_data.current_wave = static_cast<Waveform>(
                                (static_cast<int>(audio_data.current_wave) + 1) % static_cast<int>(Waveform::COUNT)
                            );
                            break;
                        case 5:
                            paused = false;
                            game_running = false;
                            break;
                    }
                }
            }
            else {
                for(int i=0; i<16; i++){
                    if(event.key.keysym.sym == keymap[i]) chip8.key[i] = 1;
                }
            }
        }
        if(event.type == SDL_KEYUP){
            for(int i=0; i<16; i++){
                if(event.key.keysym.sym == keymap[i]) chip8.key[i] = 0;
            }
        }
    }
}

int main(int argc, char* argv[]){
    (void)argc;
    (void)argv;
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0){
        std::cerr << "SDL Error: " << SDL_GetError() << std::endl;
        return 1;
    }
    if(TTF_Init() == -1) {
        std::cerr << "TTF Error: " << TTF_GetError() << std::endl;
        return 1;
    }
    TTF_Font* font = TTF_OpenFont("fonts/font.ttf", 50);
    // if(!font) {
    //    std::cerr << "Failed to load font" << std::endl;
    //     return 1;
    // }
    
    int selected_option = 0;
    int scroll_offset = 0;
    int current_theme = 0;
    int speed_level = 1;

    // Audio setup
    AudioData audio_data = {false, Waveform::SQUARE};

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 2048;
    want.callback = audio_callback;
    want.userdata = &audio_data; 

    SDL_AudioDeviceID audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if(audio_device == 0) std::cerr << "Failed to open audio: " << SDL_GetError() << std::endl;
    else SDL_PauseAudioDevice(audio_device, 0);

    SDL_Window* window = SDL_CreateWindow("Chip-8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
    if(!window){
        std::cerr << "Window error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if(!renderer){
        std::cerr << "Renderer error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Chip8 chip8;

    std::string current_rom;
    std::string save_file;

    std::vector<std::string> roms = get_roms();

    bool running = true;
    const Uint32 frame_delay = 1000 / 60;

    while(running){
        std::string selected_rom =main_menu(renderer,font,running);

        if(!running)
            break;

        if(selected_rom.empty())
            continue;

        chip8.initialise();

        chip8.load_rom(
            selected_rom
        );

        std::filesystem::path rom_path(selected_rom);

        std::string game_name = rom_path.stem().string();

        std::string save_file ="saves/" + game_name +"_save.ch8state";

        bool game_running = true;
        bool paused = false;

        selected_option = 0;
        scroll_offset = 0;

        chip8.draw_flag = true;

        while(game_running && running){
            Uint32 frame_start = SDL_GetTicks();
            handle_input(chip8, running, game_running, paused, selected_option, scroll_offset, save_file, current_theme, speed_level, audio_data);
            if(!paused){    
                int IPF = 10; 
                switch(speed_level) {
                    case 0:
                        IPF = 5;
                        break;
                    case 1:
                        IPF = 10;
                        break;
                    case 2:
                        IPF = 25;
                        break;
                }
                for(int i=0; i<IPF; i++){
                    chip8.emulate_cycle();
                }
                
                audio_data.beeping = (chip8.get_sound_timer() > 0);
                chip8.update_timers();

                // Only redraw if a draw opcode actually altered screen state
                if (chip8.draw_flag) {
                    draw_graphics(renderer, chip8, themes[current_theme]);
                    chip8.draw_flag = false;
                }
            }
            else {
                draw_paused_graphics(renderer, font, selected_option, scroll_offset, chip8, themes[current_theme]);
            }

            Uint32 frame_time = SDL_GetTicks() - frame_start;
            if (frame_time < frame_delay) {
                SDL_Delay(frame_delay - frame_time);
            }
        }
        
    }

    if(audio_device != 0) SDL_CloseAudioDevice(audio_device);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();

        return 0;
}