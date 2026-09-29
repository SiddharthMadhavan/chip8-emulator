// Exercise real menu/session code with SDL's dummy driver, without opening windows.
#include <SDL2/SDL.h>
#undef main
#define main emulator_main
#include "../src/main.cpp"
#undef main
#include "network.h"
#include <chrono>
#include <thread>
#include <cstdlib>

void require(bool ok, const char* what) {
    if (!ok) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); }
}
void press(SDL_Keycode key) {
    SDL_Event event{};
    event.type = SDL_KEYDOWN; event.key.keysym.sym = key;
    SDL_PushEvent(&event);
}
int main() {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0, "SDL init");
    require(TTF_Init() == 0, "font init");
    auto* window = SDL_CreateWindow("test", 0, 0, WIDTH, HEIGHT, SDL_WINDOW_HIDDEN);
    auto* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    auto* font = TTF_OpenFont("fonts/font.ttf", 32);
    require(window && renderer && font, "UI resources");
    bool running = true;
    press(SDLK_RETURN);
    require(main_menu(renderer, font, running) == "@host", "host menu item");
    press(SDLK_DOWN); press(SDLK_RETURN);
    require(main_menu(renderer, font, running) == "@join", "join menu item");
    press(SDLK_DOWN); press(SDLK_DOWN); press(SDLK_RETURN);
    require(main_menu(renderer, font, running).find("roms") == 0, "offline ROM menu preserved");

    std::atomic<bool> beep{false};
    std::atomic<bool> saw_frame{false}, saw_pause{false}, saw_restart{false};
    std::thread remote([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        lan::Connection guest;
        require(guest.join("127.0.0.1"), "UI guest connects");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        lan::Frame received;
        int stage = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            guest.poll();
            require(!guest.failed(), "UI host connection healthy");
            guest.send_input(1);
            if (guest.take_frame(received)) {
                if (stage == 0) {
                    saw_frame = true; press(SDLK_SPACE); stage = 1;
                } else if (stage == 1 && received.paused) {
                    saw_pause = true; press(SDLK_F2); stage = 2;
                } else if (stage == 2 && !received.paused) {
                    saw_restart = true; break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        press(SDLK_ESCAPE);
        // Keep the socket alive until the UI has consumed Escape.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });
    play_lan(window, renderer, font, true, running, beep);
    remote.join();
    require(saw_frame && saw_pause && saw_restart, "host streams, pauses, restarts");
    require(running && !beep, "leaving session preserves app and silences sound");
    press(SDLK_ESCAPE);
    play_lan(window, renderer, font, false, running, beep);
    require(running, "join address entry can be cancelled");

    std::atomic<bool> guest_moved{false}, guest_released{false};
    std::thread server([&] {
        lan::Connection host;
        require(host.host(), "guest UI server listens");
        SDL_Event text{};
        text.type = SDL_TEXTINPUT;
        SDL_strlcpy(text.text.text, "127.0.0.1", sizeof(text.text.text));
        SDL_PushEvent(&text); press(SDLK_RETURN);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        auto connected_at = deadline;
        int stage = 0;
        lan::Frame frame;
        frame.pixels.fill(1);
        while (std::chrono::steady_clock::now() < deadline) {
            host.poll();
            require(!host.failed(), "guest UI connection healthy");
            if (host.ready()) {
                host.send_frame(frame);
                if (stage == 0) { connected_at = std::chrono::steady_clock::now(); stage = 1; }
                if (stage == 1 && std::chrono::steady_clock::now() - connected_at > std::chrono::milliseconds(100)) {
                    press(SDLK_w); stage = 2;
                }
                if (stage == 2 && host.input() == 1) {
                    guest_moved = true;
                    SDL_Event lost{};
                    lost.type = SDL_WINDOWEVENT; lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
                    SDL_PushEvent(&lost); stage = 3;
                }
                if (stage == 3 && host.input() == 0) { guest_released = true; break; }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        press(SDLK_ESCAPE);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });
    play_lan(window, renderer, font, false, running, beep);
    server.join();
    require(guest_moved && guest_released, "guest sends input and releases on focus loss");
    uint32_t pixel = 0;
    SDL_Rect corner{0, 0, 1, 1};
    require(SDL_RenderReadPixels(renderer, &corner, SDL_PIXELFORMAT_ARGB8888, &pixel, sizeof(pixel)) == 0,
            "guest framebuffer readable");
    require((pixel & 0xFFFFFF) == 0xFFFFFF, "guest renders received pixels");
    TTF_CloseFont(font); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    TTF_Quit(); SDL_Quit();
    std::cout << "PASS: menus, offline selection, host/guest sessions, pause, restart, leave, focus loss, guest rendering\n";
}
