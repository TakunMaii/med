#include "Parser.h"
#include "Token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Token *parse(char *src, int *tokenCount) {
	int i = 0;
	Token *tokens = malloc(sizeof(Token) * 1024);
	*tokenCount = 0;
	while (src[i] != '\0') {
		if (src[i] == ' ' || src[i] == '\n') {
			i++;
			continue;
		} else if (src[i] == '#') {
			while (src[i] != '\n' && src[i] != '\0') {
				i++;
			}
			continue;
		} else if (src[i] >= '0' && src[i] <= '9') {
			// Parse number
			long number = 0;
			int dotSeen = 0;
			int start = i;
			while ((src[i] >= '0' && src[i] <= '9') || src[i] == '.') {
				if (src[i] == '.') {
					dotSeen = i - start;
				} else {
					number = number * 10 + (src[i] - '0');
				}
				i++;
			}
			if (!dotSeen) {
				tokens[*tokenCount].type = TOKEN_TYPE_NUMBER;
				tokens[*tokenCount].value.integer = number;
			} else {
				dotSeen = i - start - dotSeen - 1;
				tokens[*tokenCount].type = TOKEN_TYPE_FLOAT;
				double fnumber = number;
				for (int j = 0; j < dotSeen; j++) {
					fnumber /= 10.0;
				}
				tokens[*tokenCount].value.floating = fnumber;
			}
			(*tokenCount)++;
		} else if (src[i] == '\"') {
			// Parse string
			char string[256];
			int j = 0;
			i++;
			while (src[i] != '\"' && src[i] != '\0') {
				if (src[i] == '\\') {
					i++;
					if (src[i] == 'n') {
						string[j++] = '\n';
					} else if (src[i] == 't') {
						string[j++] = '\t';
					} else {
						string[j++] = src[i];
					}
				} else {
					string[j++] = src[i];
				}
				i++;
			}
			string[j] = '\0';
			tokens[*tokenCount].type = TOKEN_TYPE_STRING;
			memcpy(tokens[*tokenCount].value.string, string, strlen(string) + 1);
			(*tokenCount)++;
		} else if (src[i] == '+') {
			tokens[*tokenCount].type = TOKEN_TYPE_PLUS;
			(*tokenCount)++;
			i++;
		} else if (src[i] == '-') {
			tokens[*tokenCount].type = TOKEN_TYPE_MINUS;
			(*tokenCount)++;
			i++;
		} else if (src[i] == '*') {
			tokens[*tokenCount].type = TOKEN_TYPE_MULTIPLY;
			(*tokenCount)++;
			i++;
		} else if (src[i] == '/') {
			tokens[*tokenCount].type = TOKEN_TYPE_DIVIDE;
			(*tokenCount)++;
			i++;
		} else if (src[i] == '(') {
			tokens[*tokenCount].type = TOKEN_TYPE_LEFT_PARENTHESIS;
			(*tokenCount)++;
			i++;
		} else if (src[i] == ')') {
			tokens[*tokenCount].type = TOKEN_TYPE_RIGHT_PARENTHESIS;
			(*tokenCount)++;
			i++;
		} else if (src[i] == '[') {
			tokens[*tokenCount].type = TOKEN_TYPE_LEFT_BRACKET;
			(*tokenCount)++;
			i++;
		} else if (src[i] == ']') {
			tokens[*tokenCount].type = TOKEN_TYPE_RIGHT_BRACKET;
			(*tokenCount)++;
			i++;
		} else if (src[i] == '{') {
			tokens[*tokenCount].type = TOKEN_TYPE_LEFT_BRACE;
			(*tokenCount)++;
			i++;
		} else if (src[i] == '}') {
			tokens[*tokenCount].type = TOKEN_TYPE_RIGHT_BRACE;
			(*tokenCount)++;
			i++;
		} else if (src[i] == ',') {
			tokens[*tokenCount].type = TOKEN_TYPE_COMMA;
			(*tokenCount)++;
			i++;
		} else if (src[i] == '.') {
			tokens[*tokenCount].type = TOKEN_TYPE_DOT;
			(*tokenCount)++;
			i++;
		} else if (src[i] == '<' && src[i + 1] == '=') {
			tokens[*tokenCount].type = TOKEN_TYPE_LESS_EQUAL;
			(*tokenCount)++;
			i += 2;
		} else if (src[i] == '<') {
			tokens[*tokenCount].type = TOKEN_TYPE_LESS;
			(*tokenCount)++;
			i++;
		} else if (src[i] == '>' && src[i + 1] == '=') {
			tokens[*tokenCount].type = TOKEN_TYPE_GREATER_EQUAL;
			(*tokenCount)++;
			i += 2;
		} else if (src[i] == '>') {
			tokens[*tokenCount].type = TOKEN_TYPE_GREATER;
			(*tokenCount)++;
			i++;
		} else if (src[i] == '=' && src[i + 1] == '=') {
			tokens[*tokenCount].type = TOKEN_TYPE_EQUAL;
			(*tokenCount)++;
			i += 2;
		} else if (src[i] == '!' && src[i + 1] == '=') {
			tokens[*tokenCount].type = TOKEN_TYPE_UNEQUAL;
			(*tokenCount)++;
			i += 2;
		} else if (src[i] == '=') {
			tokens[*tokenCount].type = TOKEN_TYPE_ASSIGN;
			(*tokenCount)++;
			i++;
		} else if (src[i] == 'i' && src[i + 1] == 'f') {
			tokens[*tokenCount].type = TOKEN_TYPE_IF;
			(*tokenCount)++;
			i += 2;
		} else if (src[i] == 'e' && src[i + 1] == 'l' && src[i + 2] == 's' && src[i + 3] == 'e') {
			tokens[*tokenCount].type = TOKEN_TYPE_ELSE;
			(*tokenCount)++;
			i += 4;
		} else if (src[i] == 'w' && src[i + 1] == 'h' && src[i + 2] == 'i' && src[i + 3] == 'l' && src[i + 4] == 'e') {
			tokens[*tokenCount].type = TOKEN_TYPE_WHILE;
			(*tokenCount)++;
			i += 5;
		} else if (src[i] == 'e' && src[i + 1] == 'n' && src[i + 2] == 'd') {
			tokens[*tokenCount].type = TOKEN_TYPE_END;
			(*tokenCount)++;
			i += 3;
		}
		//function
		else if (src[i] == 'f' && src[i + 1] == 'u' && src[i + 2] == 'n' && src[i + 3] == 'c' && src[i + 4] == 't' && src[i + 5] == 'i' && src[i + 6] == 'o' && src[i + 7] == 'n') {
			tokens[*tokenCount].type = TOKEN_TYPE_FUNCTION;
			(*tokenCount)++;
			i += 8;
		}
		//return
		else if (src[i] == 'r' && src[i + 1] == 'e' && src[i + 2] == 't' && src[i + 3] == 'u' && src[i + 4] == 'r' && src[i + 5] == 'n') {
			tokens[*tokenCount].type = TOKEN_TYPE_RETURN;
			(*tokenCount)++;
			i += 6;
		}
		//break
		else if (src[i] == 'b' && src[i + 1] == 'r' && src[i + 2] == 'e' && src[i + 3] == 'a' && src[i + 4] == 'k') {
			tokens[*tokenCount].type = TOKEN_TYPE_BREAK;
			(*tokenCount)++;
			i += 5;
		}
		//continue
		else if (src[i] == 'c' && src[i + 1] == 'o' && src[i + 2] == 'n' && src[i + 3] == 't' && src[i + 4] == 'i' && src[i + 5] == 'n' && src[i + 6] == 'u' && src[i + 7] == 'e') {
			tokens[*tokenCount].type = TOKEN_TYPE_CONTINUE;
			(*tokenCount)++;
			i += 8;
		}
		//recycle
		else if (src[i] == 'r' && src[i + 1] == 'e' && src[i + 2] == 'c' && src[i + 3] == 'y' && src[i + 4] == 'c' && src[i + 5] == 'l' && src[i + 6] == 'e') {
			tokens[*tokenCount].type = TOKEN_TYPE_RECYCLE;
			(*tokenCount)++;
			i += 7;
		} else {
			// Parse identifier
			char identifier[64];
			int j = 0;
			while ((src[i] >= 'a' && src[i] <= 'z') || (src[i] >= 'A' && src[i] <= 'Z') || (src[i] >= '0' && src[i] <= '9') || src[i] == '_') {
				identifier[j++] = src[i];
				i++;
			}
			identifier[j] = '\0';
			tokens[*tokenCount].type = TOKEN_TYPE_IDENTIFIER;
			memcpy(tokens[*tokenCount].value.identifier, identifier, strlen(identifier) + 1);
			(*tokenCount)++;
		}
	}
	return tokens;
}

void printTokens(Token *tokens, int tokenCount) {
	for (int i = 0; i < tokenCount; i++) {
		switch (tokens[i].type) {
			case TOKEN_TYPE_NUMBER:
				printf("Number: %ld\n", tokens[i].value.integer);
				break;
			case TOKEN_TYPE_FLOAT:
				printf("Float: %f\n", tokens[i].value.floating);
				break;
			case TOKEN_TYPE_STRING:
				printf("String: %s\n", tokens[i].value.string);
				break;
			case TOKEN_TYPE_IDENTIFIER:
				printf("Identifier: %s\n", tokens[i].value.identifier);
				break;
			default:
				printf("Token type: %s\n", tokenTypeToString(tokens[i].type));
		}
	}
}

const char *tokenTypeToString(TokenType type) {
	switch (type) {
		case TOKEN_TYPE_NUMBER:
			return "Number";
		case TOKEN_TYPE_FLOAT:
			return "Float";
		case TOKEN_TYPE_STRING:
			return "String";
		case TOKEN_TYPE_IDENTIFIER:
			return "Identifier";
		case TOKEN_TYPE_PLUS:
			return "Plus";
		case TOKEN_TYPE_MINUS:
			return "Minus";
		case TOKEN_TYPE_MULTIPLY:
			return "Multiply";
		case TOKEN_TYPE_DIVIDE:
			return "Divide";
		case TOKEN_TYPE_LEFT_PARENTHESIS:
			return "Left Parenthesis";
		case TOKEN_TYPE_RIGHT_PARENTHESIS:
			return "Right Parenthesis";
		case TOKEN_TYPE_LEFT_BRACKET:
			return "Left Bracket";
		case TOKEN_TYPE_RIGHT_BRACKET:
			return "Right Bracket";
		case TOKEN_TYPE_LEFT_BRACE:
			return "Left Brace";
		case TOKEN_TYPE_RIGHT_BRACE:
			return "Right Brace";
		case TOKEN_TYPE_COMMA:
			return "Comma";
		case TOKEN_TYPE_DOT:
			return "Dot";
		case TOKEN_TYPE_LESS_EQUAL:
			return "Less Equal";
		case TOKEN_TYPE_LESS:
			return "Less";
		case TOKEN_TYPE_GREATER_EQUAL:
			return "Greater Equal";
		case TOKEN_TYPE_GREATER:
			return "Greater";
		case TOKEN_TYPE_EQUAL:
			return "Equal";
		case TOKEN_TYPE_UNEQUAL:
			return "Unequal";
		case TOKEN_TYPE_ASSIGN:
			return "Assign";
		case TOKEN_TYPE_IF:
			return "If";
		case TOKEN_TYPE_ELSE:
			return "Else";
		case TOKEN_TYPE_WHILE:
			return "While";
		case TOKEN_TYPE_END:
			return "End";
		case TOKEN_TYPE_FUNCTION:
			return "Function";
		case TOKEN_TYPE_RETURN:
			return "Return";
		case TOKEN_TYPE_BREAK:
			return "Break";
		case TOKEN_TYPE_CONTINUE:
			return "Continue";
		case TOKEN_TYPE_RECYCLE:
			return "Recycle";

		default:
			return "<Unknown Token>";
	}
}
