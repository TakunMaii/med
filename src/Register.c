#include "Register.h"
#include "KeyProcess.h"
#include "Mode.h"
#include <stdio.h>

#define REGISTER_SIZE 100

// 33~126
Register registers[REGISTER_SIZE] = {0};
bool registersInit = false;

Register* theRegister(char c) {
    if (c < 33 || c > 126) {
        return NULL;
    }
    return &registers[c - 33];
}

void initRegisters() {
    if (registersInit) {
        return;
    }
    for (int i = 0; i < REGISTER_SIZE; i++) {
        registers[i].length = 0;
    }
    registersInit = true;
}

void execute_register(char register_name, bool *halt, Mode *mode)
{
    initRegisters();
    Register* reg = theRegister(register_name);
    if (reg == NULL) {
        printf("ERR:Invalid register name: %c\n", register_name);
        return;
    }
    printf("INFO: Executing reg %c of length %d\n",register_name, reg->length);
    for (int i = 0;i<reg->length;i++)
    {
        pushKey(reg->content[i]);
    }    
    executeKeyBuffer(mode);
}

void pushKeyToRegister(char registerName, Key key)
{
    initRegisters();
    Register * reg = theRegister(registerName);
    reg->content[reg->length] = key;
    reg->length ++;
}

void clearRegister(char registerName)
{
    initRegisters();
    Register *reg = theRegister(registerName);
    reg->length = 0;
}

