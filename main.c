#include "KeyProcess.h"
#include "Mode.h"
#include "TextBuffer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_video.h>
#include <stdbool.h>
#include <stdio.h>
#include <threads.h>

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

bool quit = false;
TextBuffer *textBuffer;
int cursorX = 0;
int cursorY = 0;
float textPanelBiasX = 0;
float textPanelBiasY = 0;
float timer = 0;
const int lineSpace = 5;
const int TextBeginX = 10;
const int TextBeginY = 10;
enum Mode theMode = MODE_NORMAL;

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
	if (strlen(text) == 0) {
		return;
	}
	SDL_Color color = { 255, 255, 255, 255 };
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

void moveCursorUp() {
	cursorY--;
	if (cursorY < 0) {
		cursorY = 0;
	}
	if (cursorX > strlen(textBuffer->lines[cursorY]->content)) {
		cursorX = strlen(textBuffer->lines[cursorY]->content);
	}
}

void moveCursorDown() {
	cursorY++;
	if (cursorY > textBuffer->line_count - 1) {
		cursorY = textBuffer->line_count - 1;
	}
	if (cursorX > strlen(textBuffer->lines[cursorY]->content)) {
		cursorX = strlen(textBuffer->lines[cursorY]->content);
	}
}

void moveCursorLeft() {
	cursorX--;
	if (cursorX < 0 && cursorY > 0) {
		cursorY--;
		cursorX = strlen(textBuffer->lines[cursorY]->content);
	} else if (cursorX < 0) {
		cursorX = 0;
	}
}

void moveCursorRight() {
	cursorX++;
	if (cursorX > strlen(textBuffer->lines[cursorY]->content) && cursorY < textBuffer->line_count - 1) {
		cursorX = 0;
		cursorY++;
	} else if (cursorX > strlen(textBuffer->lines[cursorY]->content)) {
		cursorX = strlen(textBuffer->lines[cursorY]->content);
	}
}

void fallbackKeyProcess(Key key) {
	if (!isPrintable(key.sym)) {
		return;
	}
	if (theMode == MODE_NORMAL) {
		if (key.sym == SDLK_i) {
			theMode = MODE_INSERT;
		} else if (key.sym == SDLK_j) {
			moveCursorDown();
		} else if (key.sym == SDLK_k) {
			moveCursorUp();
		} else if (key.sym == SDLK_h) {
			moveCursorLeft();
		} else if (key.sym == SDLK_l) {
			moveCursorRight();
		}
	} else if (theMode == MODE_INSERT) {
		char keyPressed = key.sym;
		if (key.mod & KMOD_SHIFT) {
			keyPressed = toUpper(key.sym);
		}
		insertCharAt(textBuffer, cursorY, cursorX, keyPressed);
		cursorX++;
	}
}

void test() {
    theMode = MODE_NORMAL;
}

void processKeyInit() {
	registerKeyFallbackProcess(fallbackKeyProcess);
	KeyChain keychain = str2KeyChain("jj");
	registerKeyBinding(keychain, test, MODE_INSERT);
}

void processKeyEvent(SDL_Event event) {
	if (event.key.keysym.sym == SDLK_ESCAPE) {
		if (theMode != MODE_NORMAL) {
			theMode = MODE_NORMAL;
		} else {
			quit = true;
		}
	} else if (event.key.keysym.sym == SDLK_BACKSPACE) {
		if (cursorX > 0) {
			deleteCharAt(textBuffer, cursorY, cursorX - 1);
			cursorX--;
		} else if (cursorY > 0) {
			int x = strlen(textBuffer->lines[cursorY - 1]->content);
			strcpy(textBuffer->lines[cursorY - 1]->content + strlen(textBuffer->lines[cursorY - 1]->content),
					textBuffer->lines[cursorY]->content);
			deleteLineAt(textBuffer, cursorY);
			cursorY--;
			cursorX = x;
		}
	} else if (event.key.keysym.sym == SDLK_UP) {
		moveCursorUp();
	} else if (event.key.keysym.sym == SDLK_DOWN) {
		moveCursorDown();
	} else if (event.key.keysym.sym == SDLK_LEFT) {
		moveCursorLeft();
	} else if (event.key.keysym.sym == SDLK_RIGHT) {
		moveCursorRight();
	} else if (event.key.keysym.sym == SDLK_RETURN) {
		const char *restLine = textBuffer->lines[cursorY]->content + cursorX;
		insertNewLineAt(textBuffer, cursorY + 1, restLine);
		textBuffer->lines[cursorY]->content[cursorX] = '\0';
		cursorX = 0;
		cursorY++;
	} else {
		bool halt;
		processKey(event.key.keysym, &halt, theMode);
	}
}

void moveCursor(float *cursorPosX, float *cursorPosY, const TTF_Font *font) {
	*cursorPosX = TextBeginX + textPanelBiasX + cursorX * TTF_FontHeight(font) / 2.0f;
	*cursorPosY = TextBeginY + textPanelBiasY + cursorY * (TTF_FontHeight(font) + lineSpace);

	const float cursorPaddingX = 0.2f * TTF_FontHeight(font);
	const float cursorPaddingY = 2.0f * TTF_FontHeight(font);

	if (*cursorPosX < cursorPaddingX) {
		textPanelBiasX += cursorPaddingX - *cursorPosX;
	} else if (*cursorPosX > WINDOW_WIDTH - cursorPaddingX) {
		textPanelBiasX += WINDOW_WIDTH - cursorPaddingX - *cursorPosX;
	}

	if (*cursorPosY < cursorPaddingY) {
		textPanelBiasY += cursorPaddingY - *cursorPosY;
	} else if (*cursorPosY > WINDOW_HEIGHT - cursorPaddingY) {
		textPanelBiasY += WINDOW_HEIGHT - cursorPaddingY - *cursorPosY;
	}

	*cursorPosX = TextBeginX + textPanelBiasX + cursorX * TTF_FontHeight(font) / 2.0f;
	*cursorPosY = TextBeginY + textPanelBiasY + cursorY * (TTF_FontHeight(font) + lineSpace);
}

int main(int argc, char *argv[]) {
	if (argc > 1) {
		textBuffer = createTextBufferWithFile(argv[1]);
	} else {
		textBuffer = createTextBufferWith("");
	}

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
	checkptr(font);

	KEYPROCESS_Init();
	processKeyInit();

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

		const float curTime = (float)SDL_GetTicks64() / 1000;
		KEYPROCESS_Update(curTime - timer);
		timer = curTime;

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);

		float panelX = TextBeginX + textPanelBiasX;
		float panelY = TextBeginY + textPanelBiasY;
		float cursorPosX, cursorPosY;
		moveCursor(&cursorPosX, &cursorPosY, font);

		renderTextBuffer(textBuffer, renderer, font, panelX, panelY, lineSpace);
		renderCursor(renderer, font, cursorPosX, cursorPosY);

		SDL_RenderPresent(renderer);
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
