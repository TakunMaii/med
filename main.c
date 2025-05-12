#include "TextBuffer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_video.h>
#include <stdbool.h>
#include <stdio.h>

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

bool quit = false;
TextBuffer *textBuffer;
int cursorX = 0;
int cursorY = 0;
const int lineSpace = 5;
const int TextBeginX = 10;
const int TextBeginY = 10;

void checkptr(void *ptr) {
  if (ptr == NULL) {
    printf("Error: %s\n", SDL_GetError());
    exit(1);
  }
}

void checkstatus(int status) {
  if (status < 0) {
    printf("Error: %s\n", SDL_GetError());
    exit(1);
  }
}

void renderText(const char *text, SDL_Renderer *renderer, TTF_Font *font,
                float x, float y) {
  if(strlen(text) == 0) {
    return;
  }
  SDL_Color color = {255, 255, 255, 255};
  SDL_Surface *surface = TTF_RenderText_Solid(font, text, color);
  checkptr(surface);
  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
  checkptr(texture);
  SDL_Rect dstrect;
  dstrect.x = x;
  dstrect.y = y;
  dstrect.w = surface->w;
  dstrect.h = surface->h;
  SDL_FreeSurface(surface);
  SDL_RenderCopy(renderer, texture, NULL, &dstrect);
  SDL_DestroyTexture(texture);
}

void renderTextBuffer(TextBuffer *textBuffer, SDL_Renderer *renderer,
                      TTF_Font *font, float x, float y, float lineSpace) {
  float curY = y;
  for (int i = 0; i < textBuffer->line_count; i++) {
    char *line = textBuffer->lines[i]->content;
    renderText(line, renderer, font, x, curY);
    curY += TTF_FontHeight(font) + lineSpace;
  }
}

void renderCursor(SDL_Renderer *renderer, TTF_Font *font, float x, float y) {
  SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
  SDL_Rect rect;
  rect.x = x;
  rect.y = y;
  rect.w = TTF_FontHeight(font) / 10;
  rect.h = TTF_FontHeight(font);
  SDL_RenderFillRect(renderer, &rect);
}

void processKeyEvent(SDL_Event event)
{
  if (event.key.keysym.sym == SDLK_ESCAPE) {
    quit = true;
  } else if (event.key.keysym.sym == SDLK_BACKSPACE) {
      if (cursorX > 0) {
        deleteCharAt(textBuffer, cursorY, cursorX - 1);
        cursorX--;
      }
      else if (cursorY > 0) {
          ///length is not used
          int x = textBuffer->lines[cursorY - 1]->length;
          strcpy(textBuffer->lines[cursorY - 1]->content + textBuffer->lines[cursorY - 1]->length,
                textBuffer->lines[cursorY]->content);
          deleteLineAt(textBuffer, cursorY);
          cursorY--;
          cursorX = 0;
      }
  } else if (event.key.keysym.sym == SDLK_UP) {
      cursorY--;
      if (cursorY < 0) {
          cursorY = 0;
      }
  } else if (event.key.keysym.sym == SDLK_DOWN) {
      cursorY++;
      if (cursorY > textBuffer->line_count - 1) {
        cursorY = textBuffer->line_count - 1;
      }
  } else if (event.key.keysym.sym == SDLK_LEFT) {
      cursorX--;
      if (cursorX < 0 && cursorY > 0) {
        cursorY--;
        cursorX = textBuffer->lines[cursorY]->length;
      } else if (cursorX < 0) {
        cursorX = 0;
      }
  } else if (event.key.keysym.sym == SDLK_RIGHT) {
        cursorX++;
        if (cursorX > textBuffer->lines[cursorY]->length
            && cursorY < textBuffer->line_count - 1) {
          cursorX = 0;
          cursorY++;
        } else if (cursorX > textBuffer->lines[cursorY]->length) {
          cursorX = textBuffer->lines[cursorY]->length;
        }
  } else if (event.key.keysym.sym == SDLK_RETURN) {
    const char *restLine = textBuffer->lines[cursorY]->content + cursorX;
    insertNewLineAt(textBuffer, cursorY + 1, restLine);
    textBuffer->lines[cursorY]->content[cursorX] = '\0';
    cursorX = 0;
    cursorY++;
  } else {
    insertCharAt(textBuffer, cursorY, cursorX, event.key.keysym.sym);
    cursorX++;
  }
}

int main() {
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *window =
      SDL_CreateWindow("Med", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                       WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
  checkptr(window);

  SDL_Renderer *renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  checkptr(renderer);

  checkstatus(TTF_Init());

  TTF_Font *font = TTF_OpenFont("font.ttf", 24);
  textBuffer = createTextBufferWith("Hello, World!\nThis is med, aka, maii's editor");

  while (!quit) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_QUIT: {
          quit = true;
        } break;
        case SDL_KEYDOWN: {
          processKeyEvent(event);
        } break;
      }
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    renderTextBuffer(textBuffer, renderer, font, TextBeginX, TextBeginY, lineSpace);
    renderCursor(renderer, font, TextBeginX + cursorX * TTF_FontHeight(font) / 2.0f,
                 TextBeginY + cursorY * (TTF_FontHeight(font) + lineSpace));

    SDL_RenderPresent(renderer);
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
