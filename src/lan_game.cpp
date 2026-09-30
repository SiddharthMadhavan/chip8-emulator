#include "lan_game.h"
#include "network.h"
#include "chip8.h"
#include <algorithm>
#include <fstream>
#include <string>

// Shared with the offline menu.
void draw_text(SDL_Renderer*, TTF_Font*, const std::string&, int, int, SDL_Color);

namespace {
const SDL_Color white{240, 240, 240, 255}, yellow{255, 220, 70, 255};

void screen(SDL_Renderer* renderer, TTF_Font* font, const std::string& title,
            const std::string& detail, const std::string& help) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    draw_text(renderer, font, title, 50, 100, yellow);
    draw_text(renderer, font, detail, 50, 230, white);
    draw_text(renderer, font, help, 50, 370, white);
    SDL_RenderPresent(renderer);
}
bool enter_address(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* large_font, bool& running, std::string& address) {
    SDL_StartTextInput();
    bool editing = true, accepted = false;
    while (editing && running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) editing = false;
                if (event.key.keysym.sym == SDLK_RETURN && !address.empty()) {
                    accepted = true; editing = false;
                }
                if (event.key.keysym.sym == SDLK_BACKSPACE && !address.empty()) address.pop_back();
            }
            if (event.type == SDL_TEXTINPUT) {
                for (char c : std::string(event.text.text))
                    if (((c >= '0' && c <= '9') || c == '.') && address.size() < 15) address += c;
            }
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        draw_text(renderer, large_font, "JOIN LOBBY", 470, 50, yellow);
        draw_text(renderer, font, "ENTER HOST IP ADDRESS : " + address + "_", 100, 200, white);
        draw_text(renderer, font, "ENTER: CONNECT", 25, 580, white);
        draw_text(renderer, font, "ESC: EXIT", 1100, 580, white);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    SDL_StopTextInput();
    return accepted && running;
}
void show_error(SDL_Renderer* renderer, TTF_Font* font, bool& running, const std::string& error) {
    bool waiting = true;
    while (waiting && running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_RETURN)) waiting = false;
        }
        screen(renderer, font, "JOIN LOBY", error, "Enter or Esc: return to menu");
        SDL_Delay(16);
    }
}
void draw_frame(SDL_Renderer* renderer, TTF_Font* font, const lan::Frame& frame) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 64; ++x) {
        if (frame.pixels[y * 64 + x]) {
            SDL_Rect pixel{x * 20, y * 20, 20, 20};
            SDL_RenderFillRect(renderer, &pixel);
        }
    }
    if (frame.paused) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_Rect overlay{0, 250, 1280, 130};
        SDL_RenderFillRect(renderer, &overlay);
        draw_text(renderer, font, "Paused - host presses Space to resume", 100, 290, yellow);
    }
    SDL_RenderPresent(renderer);
}
}

void play_lan(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* font, TTF_Font* large_font, bool hosting, bool& running, std::atomic<bool>& beeping) {
    beeping = false;
    std::string address;
    if (!hosting && !enter_address(renderer, font, large_font, running, address))
        return;

    if (hosting) {
        std::ifstream rom("roms/Pong.ch8", std::ios::binary | std::ios::ate);
        if (!rom || rom.tellg() <= 0 || rom.tellg() > 3584) {
            show_error(renderer, font, running, "Cannot read roms/Pong.ch8"); return;
        }
    }

    lan::Connection connection;
    if (hosting)
        connection.host();
    else
        connection.join(address);

    Chip8 chip8;
    if (hosting && !connection.failed())
        chip8.load_rom("roms/Pong.ch8");
        
    lan::Frame frame;
    bool playing = true, started = false, focused = true;
    uint8_t held = 0;
    const double tick = double(SDL_GetPerformanceFrequency()) / 60.0;
    double next_tick = double(SDL_GetPerformanceCounter());
    SDL_SetWindowTitle(window, hosting
        ? "LAN Pong | Left paddle: W/S | Space: pause | F2: restart | Esc: leave"
        : "LAN Pong | Right paddle: W/S | Esc: leave");
    while (running && playing && !connection.failed()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                focused = false; held = 0;
                if (hosting && started) frame.paused = true;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                focused = true;
            if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                const auto key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) playing = false;
                if (focused && started && !frame.paused) {
                    if (key == SDLK_w) held |= 1;
                    if (key == SDLK_s) held |= 2;
                }
                if (hosting && started && key == SDLK_SPACE) {
                    frame.paused = !frame.paused; held = 0;
                }
                if (hosting && started && key == SDLK_F2) {
                    chip8.initialise(); chip8.load_rom("roms/Pong.ch8");
                    frame = {}; held = 0;
                }
            }
            if (event.type == SDL_KEYUP) {
                if (event.key.keysym.sym == SDLK_w) held &= ~1;
                if (event.key.keysym.sym == SDLK_s) held &= ~2;
            }
        }
        if (!running || !playing) break;
        connection.poll();
        if (connection.failed()) break;
        if (!connection.ready()) {
            screen(renderer, font, hosting ? "HOST GAME" : "JOIN LOBY", connection.status(), hosting ? "GUEST ENTERS YOUR IPV4 ADDRESS. ESC: CANCEL" : "Esc: cancel");
            SDL_Delay(10); continue;
        }
        if (!started) {
            started = true; held = 0;
            next_tick = double(SDL_GetPerformanceCounter());
        }
        const double now = double(SDL_GetPerformanceCounter());
        if (now >= next_tick) {
            next_tick += tick;
            if (now - next_tick > tick * 3) next_tick = now + tick;
            if (hosting) {
                std::fill(std::begin(chip8.key), std::end(chip8.key), 0);
                if (!frame.paused) {
                    chip8.key[1] = (held & 1) != 0;
                    chip8.key[4] = (held & 2) != 0;
                    chip8.key[0xC] = (connection.input() & 1) != 0;
                    chip8.key[0xD] = (connection.input() & 2) != 0;
                    for (int i = 0; i < 10; ++i) chip8.emulate_cycle();
                    frame.beep = chip8.get_sound_timer() > 0;
                    chip8.update_timers();
                } else { held = 0; frame.beep = false; }
                std::copy_n(chip8.display, frame.pixels.size(), frame.pixels.begin());
                connection.send_frame(frame);
            } else {
                if (connection.take_frame(frame) && frame.paused) held = 0;
                connection.send_input(frame.paused || !focused ? 0 : held);
            }
            beeping = frame.beep && !frame.paused;
            draw_frame(renderer, font, frame);
        }
        connection.poll();
        SDL_Delay(1);
    }
    beeping = false;
    std::fill(std::begin(chip8.key), std::end(chip8.key), 0);
    if (running && playing && connection.failed()) show_error(renderer, font, running, connection.status());
    SDL_SetWindowTitle(window, "Chip-8 Emulator");
}
