HEADERS=src/TextBuffer.h src/KeyProcess.h src/Mode.h src/FKeyFunc.h src/Common.h src/Register.h
SRC=src/main.c src/TextBuffer.c src/KeyProcess.c src/Mode.c src/Common.c src/Register.c
CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c99 -lSDL2_ttf

SDL2=`pkg-config --cflags --libs sdl2`

med: $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(SDL2) -o med $(SRC)

run:
	./med
