#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <atomic>

void play_lan(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* font, TTF_Font* large_font, bool hosting, bool& running, std::atomic<bool>& beeping);
