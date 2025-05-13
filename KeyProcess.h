#ifndef KEYPROSESS_H
#define KEYPROSESS_H

#include <SDL2/SDL_keyboard.h>
#include <stdbool.h>
#include <SDL2/SDL_keycode.h>

typedef struct
{
    int sym;
    uint16_t mod;//mod is from SDL_keymod
} Key;

typedef struct
{
    Key keys[16];
    int count;
} KeyChain;
// KeyChain is a list of keys pressed in order

typedef void(*KeyCallback)();
void(*keyFallbackProcess)(Key key);

typedef struct
{
    KeyChain chain;
    KeyCallback callback;
} KeyBinding;

// _keyBuffer's first element is the earliest key pressed
Key _keyBuffer[16] = {0};
int _keyBufferIndex = 0;

KeyBinding _keyBindings[128] = {0};
int _keyBindingCount = 0;

void pushKey(SDL_Keysym sdlkey)
{
    if (_keyBufferIndex < 16)
    {
        _keyBuffer[_keyBufferIndex].sym = sdlkey.sym;
        // regarding the mod, we only care about ctrl, shift and alt
        _keyBuffer[_keyBufferIndex].mod = sdlkey.mod & (KMOD_CTRL | KMOD_SHIFT | KMOD_ALT);
        _keyBufferIndex++;
    }
    else {
        printf("ERR: trying to push key while _keyBuffer is full\n");
    }
}

void popKey(int num)
{
    if (_keyBufferIndex > 0)
    {
        _keyBufferIndex -= num;
        if (_keyBufferIndex < 0)
        {
            _keyBufferIndex = 0;
            printf("ERR: trying to pop more keys than there are in _keyBuffer\n");
        }
    }
}

void registerKeyBinding(KeyChain chain, KeyCallback callback)
{
    if (_keyBindingCount < 128)
    {
        _keyBindings[_keyBindingCount].chain = chain;
        _keyBindings[_keyBindingCount].callback = callback;
        _keyBindingCount++;
    }
    else
    {
        printf("ERR: trying to register more key bindings than allowed\n");
    }
}

void registerKeyFallbackProcess(void (*process)(Key key))
{
    keyFallbackProcess = process;
}

bool isPrintable(int key)
{
    return key >= 32 && key <= 126;
}

bool keyEqual(Key *key1, Key *key2)
{
    return key1->sym == key2->sym && key1->mod == key2->mod;
}

KeyChain str2KeyChain(const char *str)
{
    KeyChain chain = {0};
    int i = 0;
    while (str[i] != '\0' && i < 16)
    {
        if (isPrintable(str[i]))
        {
            chain.keys[chain.count].sym = str[i];
            chain.keys[chain.count].mod = 0;
            chain.count++;
        }
        else
        {
            printf("ERR: non printable character in key chain\n");
        }
        i++;
    }
    return chain;
}

bool halfMatchKeyChain(KeyChain *chain)
{
    for (int i = 0; i < chain->count && i < _keyBufferIndex; i++)
    {
        if (!keyEqual(&chain->keys[i], &_keyBuffer[_keyBufferIndex - i - 1]))
        {
            return false;
        }
    }
    return true;
}

bool halfMatchAny()
{
    for (int i = 0; i < _keyBindingCount; i++)
    {
        if (halfMatchKeyChain(&_keyBindings[i].chain))
        {
            return true;
        }
    }
    return false;
}

bool matchKeyChain(KeyChain *chain)
{
    if(chain->count > _keyBufferIndex)
    {
        return false;
    }
    for (int i = 0; i < chain->count; i++)
    {
        if (!keyEqual(&chain->keys[i], &_keyBuffer[_keyBufferIndex - i - 1]))
        {
            return false;
        }
    }
    return true;
}

void processKey(SDL_Keysym sdlkey, bool *halt)
{
    pushKey(sdlkey);
    if (halfMatchAny())
    {
        for(int i = 0; i < _keyBindingCount; i++)
        {
             if (matchKeyChain(&_keyBindings[i].chain))
             {
                 _keyBindings[i].callback();
                 popKey(_keyBindings[i].chain.count);
                 *halt = true;
                 return;
             }
        }
    }
    *halt = false;
    if(!keyFallbackProcess)
    {
        printf("ERR: keyFallbackProcess is not set\n");
        return;
    }
    while(_keyBufferIndex > 0)
    {
        keyFallbackProcess(_keyBuffer[_keyBufferIndex - 1]);
        popKey(1);
    }
}

#endif // KEYPROSESS_H
