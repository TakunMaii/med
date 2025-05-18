HEADERS=src/TextBuffer.h src/KeyProcess.h src/Mode.h src/Common.h src/Register.h src/MedScript/Parser.h
SRC=src/main.c src/TextBuffer.c src/KeyProcess.c src/Mode.c src/Common.c src/Register.c src/MedScript/Parser.c
CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c99 -lSDL2_ttf

SDL2=`pkg-config --cflags --libs sdl2`

med: $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(SDL2) -o med $(SRC)

run:
	./med
