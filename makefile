HEADERS=TextBuffer.h KeyProcess.h Mode.h FKeyFunc.h Common.h Register.h
SRC=main.c TextBuffer.c KeyProcess.c Mode.c Common.c Register.c
CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c99 -lSDL2_ttf

SDL2=`pkg-config --cflags --libs sdl2`

med: $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(SDL2) -o med $(SRC)

run:
	./med
