#ifndef PARSER_H
#define PARSER_H

#include "Token.h"

Token* parse(char *src, int *tokenCount);

void printTokens(Token *tokens, int tokenCount);

const char* tokenTypeToString(TokenType type);

#endif
