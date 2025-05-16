#ifndef PANEL_MANAGEMENT_H
#define PANEL_MANAGEMENT_H

#include <SDL2/SDL.h>

typedef struct {
	int posX;
	int posY;
	int width;
	int height;
} Panel;

SDL_Rect panel2ClipRect(Panel panel) {
	SDL_Rect rect = {
		panel.posX,
		panel.posY,
		panel.width,
		panel.height
	};
	return rect;
}

#endif
