#ifndef KEYPROSESS_H
#define KEYPROSESS_H

#include <SDL2/SDL_keyboard.h>
#include <stdbool.h>
#include "Mode.h"
#include <SDL2/SDL_keycode.h>

typedef struct
{
    int sym;
    uint16_t mod;//mod is from SDL_keymod
} Key;

typedef struct
{
    int count;
    Key keys[16];
} KeyChain;
// KeyChain is a list of keys pressed in order

typedef void(*KeyCallback)(Key* tail);

typedef struct
{
    KeyChain chain;
    KeyCallback callback;
    int modes;
    int listenCount;
} KeyBinding;

//silly, need to change
int *getKeyBufferIndexPtr();

void keyStartWait();

Key* getKeyBuffer();

int getKeyBufferIndex();

void fallBackAllKeys();

void KEYPROCESS_Init();

void KEYPROCESS_Update(float deltime);

void pushSDLKey(SDL_Keysym sdlkey);

void pushKey(Key key);

void popKey(int num);

void registerKeyBinding(KeyChain chain, KeyCallback callback, int modes, int listenCount);

void registerKeyFallbackProcess(void (*process)(Key key));

bool isPrintable(int key);

char toUpper(char c);

bool keyEqual(Key key1, Key key2);

KeyChain str2KeyChain(const char *str);

bool halfMatchKeyChain(KeyChain *chain);

bool halfMatchAny(enum Mode theMode);

bool matchKeyChain(KeyChain chain, int listenCount);

void processKey(SDL_Keysym sdlkey, bool *halt, enum Mode theMode);

void executeKeyBuffer(enum Mode *theMode);

Key sdlKey2Key(SDL_Keysym sdlkey);

#endif // KEYPROSESS_H
