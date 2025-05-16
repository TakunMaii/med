#include "KeyProcess.h"
#include "Mode.h"
#include <SDL2/SDL_config_unix.h>
#include <stdio.h>
#include <string.h>

const float KEY_WAITING_TIME = 1.0f;
float keyTimer = 0.0f;
bool keyWaiting = false;

// _keyBuffer's first element is the earliest key pressed
Key _keyBuffer[256] = { 0 };
int _keyBufferIndex = 0;

KeyBinding _keyBindings[256] = { 0 };
int _keyBindingCount = 0;

void (*keyFallbackProcess)(Key key) = NULL;

void keyStartWait() {
	keyWaiting = true;
	keyTimer = 0.0f;
}

int getKeyBufferIndex() {
	return _keyBufferIndex;
}

Key *getKeyBuffer() {
	return _keyBuffer;
}

char toUpper(char c) {
	if (c >= 'a' && c <= 'z') {
		return c - 32;
	}
	switch (c) {
		case '`':
			return '~';
		case '1':
			return '!';
		case '2':
			return '@';
		case '3':
			return '#';
		case '4':
			return '$';
		case '5':
			return '%';
		case '6':
			return '^';
		case '7':
			return '&';
		case '8':
			return '*';
		case '9':
			return '(';
		case '0':
			return ')';
		case '-':
			return '_';
		case '=':
			return '+';
		case '[':
			return '{';
		case ']':
			return '}';
		case '\\':
			return '|';
		case ';':
			return ':';
		case '\'':
			return '"';
		case ',':
			return '<';
		case '.':
			return '>';
		case '/':
			return '?';
		default:
			return c;
	}
}

void fallBackAllKeys() {
	if (!keyFallbackProcess) {
		printf("ERR: keyFallbackProcess is not set\n");
		return;
	}
	if (_keyBufferIndex > 0) {
		for (int i = 0; i < _keyBufferIndex; i++) {
			keyFallbackProcess(_keyBuffer[i]);
		}
		_keyBufferIndex = 0;
	}
	keyWaiting = false;
}

void KEYPROCESS_Init() {
	_keyBufferIndex = 0;
	_keyBindingCount = 0;
	keyFallbackProcess = NULL;
}

void KEYPROCESS_Update(float deltime) {
	keyTimer += deltime;
	if (keyTimer > KEY_WAITING_TIME && keyWaiting) {
		keyWaiting = false;
		fallBackAllKeys();
	}
}

void pushSDLKey(SDL_Keysym sdlkey) {
	pushKey(sdlKey2Key(sdlkey));
}

void pushKey(Key key) {
	if (_keyBufferIndex < 256) {
		_keyBuffer[_keyBufferIndex] = key;
		_keyBufferIndex++;
	} else {
		printf("ERR: trying to push key while _keyBuffer is full\n");
	}
}

void popKey(int num) {
	if (_keyBufferIndex > 0) {
		_keyBufferIndex -= num;
		if (_keyBufferIndex < 0) {
			_keyBufferIndex = 0;
			printf("ERR: trying to pop more keys than there are in _keyBuffer\n");
		}
	}
}

void registerKeyBinding(KeyChain chain, KeyCallback callback, int modes) {
	if (_keyBindingCount < 256) {
		_keyBindings[_keyBindingCount].chain = chain;
		_keyBindings[_keyBindingCount].callback = callback;
		_keyBindings[_keyBindingCount].modes = modes;
		_keyBindingCount++;
	} else {
		printf("ERR: trying to register more key bindings than allowed\n");
		exit(1);
	}
}

void registerKeyFallbackProcess(void (*process)(Key key)) {
	keyFallbackProcess = process;
}

bool isPrintable(int key) {
	return key >= 32 && key <= 126;
}

bool keyEqual(Key key1, Key key2) {
	return key1.sym == key2.sym && key1.mod == key2.mod;
}

KeyChain str2KeyChain(const char *str) {
	KeyChain chain = { 0 };
	chain.count = 0;
	int i = 0;
	while (str[i] != '\0' && i < 16) {
		if (isPrintable(str[i])) {
			chain.keys[chain.count].sym = str[i];
			chain.keys[chain.count].mod = 0;
			chain.count++;
		} else {
			printf("ERR: non printable character in key chain\n");
		}
		i++;
	}
	return chain;
}

bool halfMatchKeyChain(KeyChain *chain) {
	if (_keyBufferIndex == 0) {
		return false;
	}
	for (int i = 0; i < chain->count && i < _keyBufferIndex; i++) {
		if (!keyEqual(chain->keys[i], _keyBuffer[i])) {
			return false;
		}
	}
	return true;
}

bool halfMatchAny(enum Mode theMode) {
	for (int i = 0; i < _keyBindingCount; i++) {
		if (!(_keyBindings[i].modes & theMode)) {
			continue;
		}
		if (halfMatchKeyChain(&_keyBindings[i].chain)) {
			return true;
		}
	}
	return false;
}

bool matchKeyChain(KeyChain chain) {
	if (chain.count > _keyBufferIndex) {
		return false;
	}
	for (int i = 0; i < chain.count; i++) {
		if (!keyEqual(chain.keys[i], _keyBuffer[i])) {
			return false;
		}
	}
	return true;
}

void executeKeyBuffer(enum Mode theMode) {
	while (_keyBufferIndex > 0) {
		if (!halfMatchAny(theMode)) {
			keyFallbackProcess(_keyBuffer[0]);
			memmove(_keyBuffer, _keyBuffer + 1, sizeof(Key) * (_keyBufferIndex - 1));
            _keyBufferIndex--;
		} else {
            bool matchone = false;
			for (int i = 0; i < _keyBindingCount; i++) {
				if (!(_keyBindings[i].modes & theMode)) {
					continue;
				}
				if (matchKeyChain(_keyBindings[i].chain)) {
					_keyBindings[i].callback();
					memmove(_keyBuffer, _keyBuffer + _keyBindings[i].chain.count, sizeof(Key) * (_keyBufferIndex - _keyBindings[i].chain.count));
                    _keyBufferIndex -= _keyBindings[i].chain.count;
                    matchone = true;
                    break;
				}
			}
            if (!matchone) {
                keyFallbackProcess(_keyBuffer[0]);
                memmove(_keyBuffer, _keyBuffer + 1, sizeof(Key) * (_keyBufferIndex - 1));
                _keyBufferIndex--;
            }
		}
	}
}

void processKey(SDL_Keysym sdlkey, bool *halt, enum Mode theMode) {
	pushSDLKey(sdlkey);
	if (halfMatchAny(theMode)) {
		for (int i = 0; i < _keyBindingCount; i++) {
			if (!(theMode & _keyBindings[i].modes)) {
				continue;
			}
			if (matchKeyChain(_keyBindings[i].chain)) {
				_keyBindings[i].callback();
				popKey(_keyBindings[i].chain.count);
				*halt = true;
				return;
			}
		}

		*halt = false;
		keyStartWait();
		return;
	}
	fallBackAllKeys();
	*halt = true;
}

Key sdlKey2Key(SDL_Keysym sdlkey) {
	Key key = { 0 };
	key.sym = sdlkey.sym;
	// regarding the mod, we only care about ctrl, shift and alt
	key.mod = sdlkey.mod & (KMOD_CTRL | KMOD_SHIFT | KMOD_ALT);
	return key;
}
