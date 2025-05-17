#include "Common.h"
#include "KeyProcess.h"
#include "Mode.h"
#include "PanelManagement.h"
#include "Register.h"
#include "TextBuffer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_rect.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_surface.h>
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
int pairCursorX = 0;
int pairCursorY = 0;
float textPanelBiasX = 0;
float textPanelBiasY = 0;
float timer = 0;
const int lineSpace = 5;
enum Mode theMode = MODE_NORMAL;
bool recording = false;
char recordingReg = 'q';

const Panel lineNumberPanel = { 0, 0, 35, WINDOW_HEIGHT - 50 };
const Panel statusBarPanel = { 0, WINDOW_HEIGHT - 50, WINDOW_WIDTH, 50 };
const Panel textPanel = { 35, 0, WINDOW_WIDTH - 35, WINDOW_HEIGHT - 50 };

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
		float x, float y, SDL_Rect *clipRect) {
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
	SDL_RenderSetClipRect(renderer, clipRect);
	SDL_RenderCopy(renderer, texture, NULL, &dstrect);
	SDL_DestroyTexture(texture);
}

void renderTextBuffer(TextBuffer *textBuffer, SDL_Renderer *renderer,
		TTF_Font *font, float lineSpace) {
	float x = textPanel.posX + textPanelBiasX;
	float y = textPanel.posY + textPanelBiasY;
	float curY = y;
	for (int i = 0; i < textBuffer->line_count; i++) {
		char *line = textBuffer->lines[i]->content;
		SDL_Rect clipRect = panel2ClipRect(textPanel);
		renderText(line, renderer, font, x, curY, &clipRect);
		curY += TTF_FontHeight(font) + lineSpace;
	}
}

void renderLineNumber(TextBuffer *textBuffer, SDL_Renderer *renderer,
		TTF_Font *font, float lineSpace) {
	float x = lineNumberPanel.posX;
	float y = lineNumberPanel.posY + textPanelBiasY;
	float curY = y;
	for (int i = 0; i < textBuffer->line_count; i++) {
		char lineNumber[10];
		sprintf(lineNumber, "%d", i + 1);
		SDL_Rect clipRect = panel2ClipRect(lineNumberPanel);
		renderText(lineNumber, renderer, font, x, curY, &clipRect);
		curY += TTF_FontHeight(font) + lineSpace;
	}
}

void renderStatusBar(SDL_Renderer *renderer, TTF_Font *font) {
	const char *modestr = modeToString(theMode);
	char bufferstr[20];
	int bufferlen = 0;
	for (int i = 0; i < getKeyBufferIndex(); i++) {
		bufferstr[bufferlen] = getKeyBuffer()[i].sym;
        if(getKeyBuffer()[i].mod & KMOD_SHIFT)
        {
            bufferstr[bufferlen] = toUpper(bufferstr[bufferlen]);
        }
        bufferlen++;
	}
	bufferstr[bufferlen] = '\0';

	char statusBarText[100];
	if (!recording) {
		sprintf(statusBarText, "%s | %s", modestr, bufferstr);
	} else {
		sprintf(statusBarText, "@%c | %s | %s", recordingReg, modestr, bufferstr);
	}

	SDL_Rect clipRect = panel2ClipRect(statusBarPanel);
	renderText(statusBarText, renderer, font, statusBarPanel.posX, statusBarPanel.posY, &clipRect);
}

void renderCursor(SDL_Renderer *renderer, TTF_Font *font, float x, float y) {
	SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
	SDL_Rect rect;
	rect.x = x;
	rect.y = y;
	rect.w = TTF_FontHeight(font) / 10;
	rect.h = TTF_FontHeight(font);
	SDL_RenderSetClipRect(renderer, NULL);
	SDL_RenderFillRect(renderer, &rect);
}

void renderSelectedText(SDL_Renderer *renderer, TTF_Font *font) {
	SDL_SetRenderDrawColor(renderer, 0, 105, 15, 125);
	SDL_RenderSetClipRect(renderer, NULL);

	if (theMode == MODE_VISUAL) {
		int x1 = cursorX, y1 = cursorY;
		int x2 = pairCursorX, y2 = pairCursorY;
		if (y1 > y2 || (y1 == y2 && x1 > x2)) {
			swap(&x1, &x2);
			swap(&y1, &y2);
		}
		for (int i = y1; i <= y2; i++) {
			int startX = (i == y1) ? x1 : 0;
			int endX = (i == y2) ? x2 : strlen(textBuffer->lines[i]->content);
			SDL_Rect rect;
			rect.x = textPanel.posX + textPanelBiasX + startX * TTF_FontHeight(font) / 2.0f;
			rect.y = textPanel.posY + textPanelBiasY + i * (TTF_FontHeight(font) + lineSpace);
			rect.w = (endX - startX) * TTF_FontHeight(font) / 2;
			rect.h = TTF_FontHeight(font);
			SDL_RenderFillRect(renderer, &rect);
		}
	} else if (theMode == MODE_VISUAL_BLOCK) {
		int x1 = cursorX, y1 = cursorY;
		int x2 = pairCursorX, y2 = pairCursorY;
		if (y1 > y2) {
			swap(&y1, &y2);
		}
		if (x1 > x2) {
			swap(&x1, &x2);
		}
		for (int i = y1; i <= y2; i++) {
			int linelen = strlen(textBuffer->lines[i]->content);
			if (x1 > linelen) {
				continue;
			}
			int startX = x1;
			int endX = (x2 < linelen) ? x2 : linelen;
			SDL_Rect rect;
			rect.x = textPanel.posX + textPanelBiasX + startX * TTF_FontHeight(font) / 2.0f;
			rect.y = textPanel.posY + textPanelBiasY + i * (TTF_FontHeight(font) + lineSpace);
			rect.w = (endX - startX) * TTF_FontHeight(font) / 2;
			rect.h = TTF_FontHeight(font);
			SDL_RenderFillRect(renderer, &rect);
		}
	} else if (theMode == MODE_VISUAL_LINE) {
		int x1 = cursorX, y1 = cursorY;
		int x2 = pairCursorX, y2 = pairCursorY;
		if (y1 > y2) {
			swap(&y1, &y2);
		}
		for (int i = y1; i <= y2; i++) {
			SDL_Rect rect;
			rect.x = textPanel.posX + textPanelBiasX;
			rect.y = textPanel.posY + textPanelBiasY + i * (TTF_FontHeight(font) + lineSpace);
			rect.w = textPanel.width;
			rect.h = TTF_FontHeight(font);
			SDL_RenderFillRect(renderer, &rect);
		}
	}
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

void cursorFind(Key* tail) {
    char c = tail[0].sym;
	if (cursorX >= strlen(textBuffer->lines[cursorY]->content)) {
		return;
	}
	if (GetCharAt(textBuffer, cursorY, cursorX) == c) {
		cursorX++;
	}
	while (GetCharAt(textBuffer, cursorY, cursorX) != c &&
			cursorX < strlen(textBuffer->lines[cursorY]->content)) {
		cursorX++;
	}
}

bool isLetter(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isStartOfWordNow() {
	return isLetter(GetCharAt(textBuffer, cursorY, cursorX)) &&
			(cursorX == 0 || !isLetter(GetCharAt(textBuffer, cursorY, cursorX - 1)));
}

bool isEndOfWordNow() {
	return isLetter(GetCharAt(textBuffer, cursorY, cursorX)) &&
			(cursorX == strlen(textBuffer->lines[cursorY]->content) - 1 ||
					!isLetter(GetCharAt(textBuffer, cursorY, cursorX + 1)));
}

bool isAtEndOfTextBuffer() {
	return cursorY == textBuffer->line_count - 1 && cursorX == strlen(textBuffer->lines[cursorY]->content);
}

bool isAtStartOfTextBuffer() {
	return cursorY == 0 && cursorX == 0;
}

void moveCursorLeftWord() {
	if (isAtStartOfTextBuffer()) {
		return;
	}
	if (isStartOfWordNow()) {
		moveCursorLeft();
	}
	while (!isStartOfWordNow() && !isAtStartOfTextBuffer()) {
		moveCursorLeft();
	}
}

void moveCursorRightWord() {
	if (isAtEndOfTextBuffer()) {
		return;
	}
	if (isStartOfWordNow()) {
		moveCursorRight();
	}
	while (!isStartOfWordNow() && !isAtEndOfTextBuffer()) {
		moveCursorRight();
	}
}

void moveCursorToEndofWord() {
	if (isAtEndOfTextBuffer()) {
		return;
	}
	if (isEndOfWordNow()) {
		moveCursorRight();
	}
	while (!isEndOfWordNow() && !isAtEndOfTextBuffer()) {
		moveCursorRight();
	}
}

void fallbackKeyProcess(Key key) {
	if (key.sym == SDLK_ESCAPE) {
		if (theMode != MODE_NORMAL) {
			theMode = MODE_NORMAL;
		} else {
			quit = true;
		}
	} else if (key.sym == SDLK_BACKSPACE) {
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
	} else if (key.sym == SDLK_UP) {
		moveCursorUp();
	} else if (key.sym == SDLK_DOWN) {
		moveCursorDown();
	} else if (key.sym == SDLK_LEFT) {
		moveCursorLeft();
	} else if (key.sym == SDLK_RIGHT) {
		moveCursorRight();
	} else if (key.sym == SDLK_RETURN) {
		const char *restLine = textBuffer->lines[cursorY]->content + cursorX;
		insertNewLineAt(textBuffer, cursorY + 1, restLine);
		textBuffer->lines[cursorY]->content[cursorX] = '\0';
		cursorX = 0;
		cursorY++;
	} else if (theMode & (MODE_NORMAL | MODE_VISUAL | MODE_VISUAL_BLOCK | MODE_VISUAL_LINE)) {
		if (key.sym == SDLK_i) {
			theMode = MODE_INSERT;
		} else if (key.sym == SDLK_a && key.mod & KMOD_SHIFT) {
			theMode = MODE_INSERT;
			cursorX = strlen(textBuffer->lines[cursorY]->content);
		} else if (key.sym == SDLK_a) {
			theMode = MODE_INSERT;
			cursorX++;
			if (cursorX > strlen(textBuffer->lines[cursorY]->content)) {
				cursorX = strlen(textBuffer->lines[cursorY]->content);
			}
		} else if (key.sym == SDLK_j) {
			moveCursorDown();
		} else if (key.sym == SDLK_k) {
			moveCursorUp();
		} else if (key.sym == SDLK_o && key.mod & KMOD_SHIFT) {
            insertNewLineAt(textBuffer, cursorY, "");
            cursorX = 0;
		} else if (key.sym == SDLK_o) {
            insertNewLineAt(textBuffer, cursorY + 1, "");
            cursorY ++;
            cursorX = 0;
		} else if (key.sym == SDLK_h) {
			moveCursorLeft();
		} else if (key.sym == SDLK_x) {
            deleteCharAt(textBuffer, cursorY, cursorX);
		} else if (key.sym == SDLK_l) {
			moveCursorRight();
		} else if (key.sym == SDLK_w) {
			moveCursorRightWord();
		} else if (key.sym == SDLK_b) {
			moveCursorLeftWord();
		} else if (key.sym == SDLK_e) {
			moveCursorToEndofWord();
		} else if (key.sym == SDLK_g && key.mod & KMOD_SHIFT) {
			cursorX = 0;
			cursorY = textBuffer->line_count - 1;
		} else if (key.sym == SDLK_q) {
			recording = false;
		} else if (theMode == MODE_NORMAL) {
			if (key.sym == SDLK_v) {
				if (key.mod & KMOD_CTRL) {
					theMode = MODE_VISUAL_BLOCK;
					pairCursorX = cursorX;
					pairCursorY = cursorY;
				} else if (key.mod & KMOD_SHIFT) {
					theMode = MODE_VISUAL_LINE;
					pairCursorX = cursorX;
					pairCursorY = cursorY;
				} else {
					theMode = MODE_VISUAL;
					pairCursorX = cursorX;
					pairCursorY = cursorY;
				}
			}
		} else {
			if (key.sym == SDLK_d) {
				deleteBetween(textBuffer, cursorX, cursorY, pairCursorX, pairCursorY, theMode);
				theMode = MODE_NORMAL;
			}
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

void startRecord(Key* tail) {
    char reg = tail[0].sym;
	recording = true;
	recordingReg = reg;
	clearRegister(recordingReg);
}

void executeRegister(Key* tail) {
    char reg = tail[0].sym;
	bool halt = false;
	// Before executing the register, we remove the 2 keys that
	// we just press triggered this function, this is so naive
	// that we need to change this.
	*getKeyBufferIndexPtr() -= 2;
	if (*getKeyBufferIndexPtr() < 0) {
		printf("ERR: Unknown situation occured!\n");
		exit(1);
	}
	execute_register(reg, &halt, &theMode);
}

void goToFirstPlace() {
	cursorX = 0;
	cursorY = 0;
}

void deleteCurLine() {
    if (cursorY < textBuffer->line_count) {
        deleteLineAt(textBuffer, cursorY);
    }
}

void processKeyInit() {
	registerKeyFallbackProcess(fallbackKeyProcess);
	{
		KeyChain keychain = str2KeyChain("jj");
		registerKeyBinding(keychain, test, MODE_INSERT, 0);
	}
	{
		KeyChain keychain = str2KeyChain("gg");
		registerKeyBinding(keychain, goToFirstPlace, MODE_NORMAL, 0);
	}
	{
		KeyChain keychain = str2KeyChain("dd");
		registerKeyBinding(keychain, deleteCurLine, MODE_NORMAL, 0);
	}
	{
		KeyChain keychain = str2KeyChain("f");
		registerKeyBinding(keychain, cursorFind, MODE_NORMAL, 1);
	}
	{
		KeyChain keychain = str2KeyChain("q");
		registerKeyBinding(keychain, startRecord, MODE_NORMAL, 1);
	}
	{
		KeyChain keychain = str2KeyChain("@");
		registerKeyBinding(keychain, executeRegister, MODE_NORMAL, 1);
	}
}

void processKeyEvent(SDL_Event event) {
	// recording key to register if needed
	bool halt = false;
	if (recording && event.key.keysym.sym != SDLK_q) {
		pushKeyToRegister(recordingReg, sdlKey2Key(event.key.keysym));
	}
	processKey(event.key.keysym, &halt, theMode);
}

void moveCursor(float *cursorPosX, float *cursorPosY, const TTF_Font *font) {
	*cursorPosX = textPanel.posX + textPanelBiasX + cursorX * TTF_FontHeight(font) / 2.0f;
	*cursorPosY = textPanel.posY + textPanelBiasY + cursorY * (TTF_FontHeight(font) + lineSpace);

	const float cursorPaddingX = 0.2f * TTF_FontHeight(font);
	const float cursorPaddingY = 2.0f * TTF_FontHeight(font);

	//if the cursor is out of the window, move the text panel
	if (*cursorPosX - textPanel.posX < cursorPaddingX) {
		textPanelBiasX += cursorPaddingX - *cursorPosX + textPanel.posX;
	} else if (*cursorPosX - textPanel.posX > textPanel.width - cursorPaddingX) {
		textPanelBiasX += textPanel.width - cursorPaddingX - *cursorPosX + textPanel.posX;
	}

	if (*cursorPosY - textPanel.posY < cursorPaddingY) {
		textPanelBiasY += cursorPaddingY - *cursorPosY + textPanel.posY;
	} else if (*cursorPosY - textPanel.posY > textPanel.height - cursorPaddingY) {
		textPanelBiasY += textPanel.height - cursorPaddingY - *cursorPosY + textPanel.posY;
	}

	//if the text penel is too low, move it up
	//this may happen when the cursor is at the top lines of the text
	if (textPanelBiasY > 0) {
		textPanelBiasY = 0;
	}

	*cursorPosX = textPanel.posX + textPanelBiasX + cursorX * TTF_FontHeight(font) / 2.0f;
	*cursorPosY = textPanel.posY + textPanelBiasY + cursorY * (TTF_FontHeight(font) + lineSpace);
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

		float cursorPosX, cursorPosY;
		moveCursor(&cursorPosX, &cursorPosY, font);

		if (theMode == MODE_VISUAL || theMode == MODE_VISUAL_BLOCK || theMode == MODE_VISUAL_LINE) {
			renderSelectedText(renderer, font);
		}

		renderTextBuffer(textBuffer, renderer, font, lineSpace);
		renderLineNumber(textBuffer, renderer, font, lineSpace);
		renderCursor(renderer, font, cursorPosX, cursorPosY);
		renderStatusBar(renderer, font);

		SDL_RenderPresent(renderer);
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
