#ifndef REGISTER_H
#define REGISTER_H

#include "KeyProcess.h"

typedef struct {
    Key content[256];
    int length;
} Register;

void execute_register(char register_name, bool *halt, Mode *mode);

void pushKeyToRegister(char registerName, Key key);

void clearRegister(char registerName);

#endif
