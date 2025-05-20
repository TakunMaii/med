#include "AST.h"
#include "Parser.h"
#include "Table.h"
#include "Token.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	ASTNode *content[128];
	int ptr;
} ASTNodeStack;

ASTNodeStack *createASTNodeStack(void) {
	ASTNodeStack *stack = malloc(sizeof(ASTNodeStack));
	stack->ptr = 0;
	return stack;
}

void pushASTNode(ASTNodeStack *stack, ASTNode *node) {
	if (stack->ptr < 128) {
		stack->content[stack->ptr] = node;
		stack->ptr++;
	} else {
		printf("ERR: ASTNodeStack overflow\n");
		exit(1);
	}
}

ASTNode *popASTNode(ASTNodeStack *stack) {
	if (stack->ptr > 0) {
		stack->ptr--;
		return stack->content[stack->ptr];
	} else {
		printf("ERR: ASTNodeStack underflow\n");
		exit(1);
		return NULL;
	}
}

void freeASTNodeStack(ASTNodeStack *stack) {
	for (int i = 0; i < stack->ptr; i++) {
		free(stack->content[i]);
	}
	free(stack);
}

TableUnitType ASTVariableTypeToTableUnitType(VariableType type) {
	switch (type) {
		case VARIABLE_TYPE_INTEGER:
			return TABLE_UNIT_INTEGER;
		case VARIABLE_TYPE_FLOAT:
			return TABLE_UNIT_FLOATING;
		case VARIABLE_TYPE_STRING:
			return TABLE_UNIT_STRING;
		case VARIABLE_TYPE_TABLE:
			return TABLE_UNIT_TABLE;
		case VARIABLE_TYPE_FUNCTION:
			return TABLE_UNIT_FUNCTION;
		case VARIABLE_TYPE_NULL:
			return TABLE_UNIT_NULL;
		default:
			return TABLE_UNIT_NULL;
	}
}

VariableType TableUnitTypeToASTVariableType(TableUnitType type) {
	switch (type) {
		case TABLE_UNIT_INTEGER:
			return VARIABLE_TYPE_INTEGER;
		case TABLE_UNIT_FLOATING:
			return VARIABLE_TYPE_FLOAT;
		case TABLE_UNIT_STRING:
			return VARIABLE_TYPE_STRING;
		case TABLE_UNIT_TABLE:
			return VARIABLE_TYPE_TABLE;
		case TABLE_UNIT_FUNCTION:
			return VARIABLE_TYPE_FUNCTION;
		case TABLE_UNIT_NULL:
			return VARIABLE_TYPE_NULL;
		default:
			return VARIABLE_TYPE_NULL;
	}
}
// ------------

// Shunting Yard Algorithm
// reference: https://cloud.tencent.com/developer/article/1607240
// TODO: problem to be fixed: consider the tokens are : 2 2 + + 2
ASTNode *tryFindValueNode(Token *tokens, int tokenCount, bool *success, int *stepForward) {
	if (tokenCount == 0) {
		*success = true;
		return NULL;
	}

	if (tokens[0].type == TOKEN_TYPE_FUNCTION) {
		int i = 2; // +2 for the function and left parenthesis
		ASTNode *functionEnter = malloc(sizeof(ASTNode));
		functionEnter->type = AST_FUNCTION_ENTER;
		functionEnter->data.functionEnter.parameterCount = 0;
		while (i < tokenCount && tokens[i].type != TOKEN_TYPE_RIGHT_PARENTHESIS) {
			if (tokens[i].type != TOKEN_TYPE_IDENTIFIER) {
				printf("Error: expected identifier\n");
				break;
			}
			strcpy(functionEnter->data.functionEnter.parameterNames[functionEnter->data.functionEnter.parameterCount++], tokens[i].value.identifier);
			i++;
			if (tokens[i].type == TOKEN_TYPE_COMMA) {
				i++;
			} else if (tokens[i].type != TOKEN_TYPE_RIGHT_PARENTHESIS) {
				printf("Error: expected comma or right parenthesis\n");
				break;
			}
		}
		if (i == tokenCount) {
			printf("ERR: no matching right parenthesis\n");
			exit(1);
		}
		i++; // +1 for the right parenthesis
		bool localSuccess;
		int localStepForward;
		functionEnter->data.functionEnter.body = tokens2AST(tokens + i, tokenCount - i, &localSuccess, &localStepForward);
		i += localStepForward;
		*stepForward = i;
		return functionEnter;
	}

	*stepForward = 0;
	*success = false;

	ASTNodeStack *valueStack = createASTNodeStack();
	ASTNodeStack *operatorStack = createASTNodeStack();

	while (*stepForward < tokenCount) {
		Token token = tokens[*stepForward];
		if (token.type == TOKEN_TYPE_IDENTIFIER) {
			ASTNode *valueNode = malloc(sizeof(ASTNode));
			valueNode->type = AST_FETCH_VARIABLE;
			strcpy(valueNode->data.fetchVariable.variableName, token.value.identifier);
			pushASTNode(valueStack, valueNode);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_NUMBER) {
			ASTNode *numberNode = malloc(sizeof(ASTNode));
			numberNode->type = AST_VALUE;
			numberNode->data.value.type = VARIABLE_TYPE_INTEGER;
			numberNode->data.value.value.integer = token.value.integer;
			pushASTNode(valueStack, numberNode);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_FLOAT) {
			ASTNode *numberNode = malloc(sizeof(ASTNode));
			numberNode->type = AST_VALUE;
			numberNode->data.value.type = VARIABLE_TYPE_FLOAT;
			numberNode->data.value.value.floating = token.value.floating;
			pushASTNode(valueStack, numberNode);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_STRING) {
			ASTNode *numberNode = malloc(sizeof(ASTNode));
			numberNode->type = AST_VALUE;
			numberNode->data.value.type = VARIABLE_TYPE_STRING;
            strcpy(numberNode->data.value.value.string, token.value.string);
			pushASTNode(valueStack, numberNode);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_NOT) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_NOT;
			pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_OR) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_OR;

			if (operatorStack->ptr > 0) {
				ASTNode *topOperator = operatorStack->content[operatorStack->ptr - 1];
				if (topOperator->data.calculate.operation == TOKEN_TYPE_DIVIDE || topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY || topOperator->data.calculate.operation == TOKEN_TYPE_PLUS || topOperator->data.calculate.operation == TOKEN_TYPE_MINUS || topOperator->data.calculate.operation == TOKEN_TYPE_LESS || topOperator->data.calculate.operation == TOKEN_TYPE_GREATER || topOperator->data.calculate.operation == TOKEN_TYPE_EQUAL || topOperator->data.calculate.operation == TOKEN_TYPE_UNEQUAL || topOperator->data.calculate.operation == TOKEN_TYPE_LESS_EQUAL || topOperator->data.calculate.operation == TOKEN_TYPE_GREATER_EQUAL || topOperator->data.calculate.operation == TOKEN_TYPE_AND) {
					ASTNode *value1 = popASTNode(valueStack);
					ASTNode *value2 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value2;
					topOperator->data.calculate.rightOperand = value1;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				} else if (topOperator->data.calculate.operation == TOKEN_TYPE_NOT) {
					ASTNode *value1 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value1;
					topOperator->data.calculate.rightOperand = NULL;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				}
			}

			pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_AND) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_AND;

			if (operatorStack->ptr > 0) {
				ASTNode *topOperator = operatorStack->content[operatorStack->ptr - 1];
				if (topOperator->data.calculate.operation == TOKEN_TYPE_DIVIDE || topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY || topOperator->data.calculate.operation == TOKEN_TYPE_PLUS || topOperator->data.calculate.operation == TOKEN_TYPE_MINUS || topOperator->data.calculate.operation == TOKEN_TYPE_LESS || topOperator->data.calculate.operation == TOKEN_TYPE_GREATER || topOperator->data.calculate.operation == TOKEN_TYPE_EQUAL || topOperator->data.calculate.operation == TOKEN_TYPE_UNEQUAL || topOperator->data.calculate.operation == TOKEN_TYPE_LESS_EQUAL || topOperator->data.calculate.operation == TOKEN_TYPE_GREATER_EQUAL) {
					ASTNode *value1 = popASTNode(valueStack);
					ASTNode *value2 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value2;
					topOperator->data.calculate.rightOperand = value1;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				} else if (topOperator->data.calculate.operation == TOKEN_TYPE_NOT) {
					ASTNode *value1 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value1;
					topOperator->data.calculate.rightOperand = NULL;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				}
			}

			pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_UNEQUAL) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_UNEQUAL;

			if (operatorStack->ptr > 0) {
				ASTNode *topOperator = operatorStack->content[operatorStack->ptr - 1];
				if (topOperator->data.calculate.operation == TOKEN_TYPE_DIVIDE || topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY || topOperator->data.calculate.operation == TOKEN_TYPE_PLUS || topOperator->data.calculate.operation == TOKEN_TYPE_MINUS) {
					ASTNode *value1 = popASTNode(valueStack);
					ASTNode *value2 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value2;
					topOperator->data.calculate.rightOperand = value1;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				} else if (topOperator->data.calculate.operation == TOKEN_TYPE_NOT) {
					ASTNode *value1 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value1;
					topOperator->data.calculate.rightOperand = NULL;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				}
			}

			pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_EQUAL) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_EQUAL;

			if (operatorStack->ptr > 0) {
				ASTNode *topOperator = operatorStack->content[operatorStack->ptr - 1];
				if (topOperator->data.calculate.operation == TOKEN_TYPE_DIVIDE || topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY || topOperator->data.calculate.operation == TOKEN_TYPE_PLUS || topOperator->data.calculate.operation == TOKEN_TYPE_MINUS) {
					ASTNode *value1 = popASTNode(valueStack);
					ASTNode *value2 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value2;
					topOperator->data.calculate.rightOperand = value1;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				} else if (topOperator->data.calculate.operation == TOKEN_TYPE_NOT) {
					ASTNode *value1 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value1;
					topOperator->data.calculate.rightOperand = NULL;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				}
			}

			pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_LESS_EQUAL) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_LESS_EQUAL;

			if (operatorStack->ptr > 0) {
				ASTNode *topOperator = operatorStack->content[operatorStack->ptr - 1];
				if (topOperator->data.calculate.operation == TOKEN_TYPE_DIVIDE || topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY || topOperator->data.calculate.operation == TOKEN_TYPE_PLUS || topOperator->data.calculate.operation == TOKEN_TYPE_MINUS) {
					ASTNode *value1 = popASTNode(valueStack);
					ASTNode *value2 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value2;
					topOperator->data.calculate.rightOperand = value1;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				} else if (topOperator->data.calculate.operation == TOKEN_TYPE_NOT) {
					ASTNode *value1 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value1;
					topOperator->data.calculate.rightOperand = NULL;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				}
			}

			pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_LESS) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_LESS;

			if (operatorStack->ptr > 0) {
				ASTNode *topOperator = operatorStack->content[operatorStack->ptr - 1];
				if (topOperator->data.calculate.operation == TOKEN_TYPE_DIVIDE || topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY || topOperator->data.calculate.operation == TOKEN_TYPE_PLUS || topOperator->data.calculate.operation == TOKEN_TYPE_MINUS) {
					ASTNode *value1 = popASTNode(valueStack);
					ASTNode *value2 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value2;
					topOperator->data.calculate.rightOperand = value1;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				} else if (topOperator->data.calculate.operation == TOKEN_TYPE_NOT) {
					ASTNode *value1 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value1;
					topOperator->data.calculate.rightOperand = NULL;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				}
			}

			pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_GREATER_EQUAL) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_GREATER_EQUAL;

			if (operatorStack->ptr > 0) {
				ASTNode *topOperator = operatorStack->content[operatorStack->ptr - 1];
				if (topOperator->data.calculate.operation == TOKEN_TYPE_DIVIDE || topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY || topOperator->data.calculate.operation == TOKEN_TYPE_PLUS || topOperator->data.calculate.operation == TOKEN_TYPE_MINUS) {
					ASTNode *value1 = popASTNode(valueStack);
					ASTNode *value2 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value2;
					topOperator->data.calculate.rightOperand = value1;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				} else if (topOperator->data.calculate.operation == TOKEN_TYPE_NOT) {
					ASTNode *value1 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value1;
					topOperator->data.calculate.rightOperand = NULL;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				}
			}

			pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_GREATER) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_GREATER;

			if (operatorStack->ptr > 0) {
				ASTNode *topOperator = operatorStack->content[operatorStack->ptr - 1];
				if (topOperator->data.calculate.operation == TOKEN_TYPE_DIVIDE || topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY || topOperator->data.calculate.operation == TOKEN_TYPE_PLUS || topOperator->data.calculate.operation == TOKEN_TYPE_MINUS) {
					ASTNode *value1 = popASTNode(valueStack);
					ASTNode *value2 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value2;
					topOperator->data.calculate.rightOperand = value1;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				} else if (topOperator->data.calculate.operation == TOKEN_TYPE_NOT) {
					ASTNode *value1 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value1;
					topOperator->data.calculate.rightOperand = NULL;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				}
			}

			pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_PLUS) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_PLUS;

			if (operatorStack->ptr > 0) {
				ASTNode *topOperator = operatorStack->content[operatorStack->ptr - 1];
				if (topOperator->data.calculate.operation == TOKEN_TYPE_DIVIDE || topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY) {
					ASTNode *value1 = popASTNode(valueStack);
					ASTNode *value2 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value2;
					topOperator->data.calculate.rightOperand = value1;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				} else if (topOperator->data.calculate.operation == TOKEN_TYPE_NOT) {
					ASTNode *value1 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value1;
					topOperator->data.calculate.rightOperand = NULL;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				}
			}

			pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_MINUS) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_MINUS;

			if (operatorStack->ptr > 0) {
				ASTNode *topOperator = operatorStack->content[operatorStack->ptr - 1];
				if (topOperator->data.calculate.operation == TOKEN_TYPE_DIVIDE || topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY) {
					ASTNode *value1 = popASTNode(valueStack);
					ASTNode *value2 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value2;
					topOperator->data.calculate.rightOperand = value1;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				} else if (topOperator->data.calculate.operation == TOKEN_TYPE_NOT) {
					ASTNode *value1 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value1;
					topOperator->data.calculate.rightOperand = NULL;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				}
			}

			pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_MULTIPLY) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_MULTIPLY;
			if (operatorStack->ptr > 0) {
				ASTNode *topOperator = operatorStack->content[operatorStack->ptr - 1];
				if (topOperator->data.calculate.operation == TOKEN_TYPE_NOT) {
					ASTNode *value1 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value1;
					topOperator->data.calculate.rightOperand = NULL;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				}
			}
			pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_DIVIDE) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_DIVIDE;
			if (operatorStack->ptr > 0) {
				ASTNode *topOperator = operatorStack->content[operatorStack->ptr - 1];
				if (topOperator->data.calculate.operation == TOKEN_TYPE_NOT) {
					ASTNode *value1 = popASTNode(valueStack);
					topOperator->data.calculate.leftOperand = value1;
					topOperator->data.calculate.rightOperand = NULL;
					popASTNode(operatorStack);
					pushASTNode(valueStack, topOperator);
				}
			}
			pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_LEFT_PARENTHESIS) {
			int j = *stepForward + 1;
			while (j < tokenCount && tokens[j].type != TOKEN_TYPE_RIGHT_PARENTHESIS) {
				j++;
			}
			if (j == tokenCount) {
				printf("ERR: no matching right parenthesis\n");
				exit(1);
			}
			bool innerSuccess;
			int innerStepForward;
			ASTNode *innerNode = tryFindValueNode(tokens + *stepForward + 1, j - *stepForward - 1, &innerSuccess, &innerStepForward);
			if (innerStepForward != j - *stepForward - 1) {
				printf("ERR: inner step forward is not equal to j - *stepForward - 1\n");
				exit(1);
			}
			if (!innerSuccess) {
				printf("ERR: inner success is false\n");
				exit(1);
			}
			(*stepForward) += innerStepForward + 2; // +2 for the left and right parenthesis
			pushASTNode(valueStack, innerNode);
		} else {
			// encounter an non-calculation token, which means
			// here is the end of the expression
			break;
		}
	}

	printf("INFO: ASTNodeStack valueStack size is %d\n", valueStack->ptr);
	printf("INFO: ASTNodeStack operatorStack size is %d\n", operatorStack->ptr);
	while (operatorStack->ptr > 0) {
		ASTNode *topOperator = popASTNode(operatorStack);
		if (topOperator->data.calculate.operation == TOKEN_TYPE_NOT) {
			ASTNode *value1 = popASTNode(valueStack);
			topOperator->data.calculate.leftOperand = value1;
			topOperator->data.calculate.rightOperand = NULL;
			pushASTNode(valueStack, topOperator);
		} else {
			ASTNode *value1 = popASTNode(valueStack);
			ASTNode *value2 = popASTNode(valueStack);
			topOperator->data.calculate.leftOperand = value2;
			topOperator->data.calculate.rightOperand = value1;
			pushASTNode(valueStack, topOperator);
		}
	}

	*success = true;
	if (valueStack->ptr != 1) {
		printf("WARN: ASTNodeStack valueStack size is not 1\n");
	}
	ASTNode *res = popASTNode(valueStack);
	res->next = NULL;
	free(operatorStack);
	free(valueStack);
	return res;
}

ASTNode *tokens2AST(Token *tokens, int tokenCount, bool *success, int *stepForward) {
	printf("INFO: NEW Code Block Start\n");
	ASTNode *root = malloc(sizeof(ASTNode));
	root->type = AST_BLOCK_START;
	root->data.blockStart.controlBlock = malloc(sizeof(CodeBlockControlBlock));
	root->data.blockStart.controlBlock->localVariables = tableCreate();

	ASTNode *current = root;

	int i = 0;
	while (i < tokenCount) {
		if (tokens[i].type == TOKEN_TYPE_FUNCTION) {
			ASTNode *assignment = malloc(sizeof(ASTNode));
			assignment->type = AST_ASSIGN_VARIABLE;
			i++; //jump FUNCITON token
			strcpy(assignment->data.assignVariable.variableName, tokens[i].value.identifier);
			bool success;
			int stepForward;

			i += 2; // +2 for the identifier and left parenthesis
			ASTNode *functionEnter = malloc(sizeof(ASTNode));
			functionEnter->type = AST_FUNCTION_ENTER;
			functionEnter->data.functionEnter.parameterCount = 0;
			while (i < tokenCount && tokens[i].type != TOKEN_TYPE_RIGHT_PARENTHESIS) {
				if (tokens[i].type != TOKEN_TYPE_IDENTIFIER) {
					printf("Error: expected identifier\n");
					break;
				}
				strcpy(functionEnter->data.functionEnter.parameterNames[functionEnter->data.functionEnter.parameterCount++], tokens[i].value.identifier);
				i++;
				if (tokens[i].type == TOKEN_TYPE_COMMA) {
					i++;
				} else if (tokens[i].type != TOKEN_TYPE_RIGHT_PARENTHESIS) {
					printf("Error: expected comma or right parenthesis\n");
					break;
				}
			}
			if (i == tokenCount) {
				printf("ERR: no matching right parenthesis\n");
				exit(1);
			}
			i++; // +1 for the right parenthesis
			functionEnter->data.functionEnter.body = tokens2AST(tokens + i, tokenCount - i, &success, &stepForward);
			i += stepForward;
			assignment->data.assignVariable.valueNode = functionEnter;
			current->next = assignment;
			current = assignment;
		} else if (tokens[i].type == TOKEN_TYPE_IDENTIFIER && tokens[i + 1].type == TOKEN_TYPE_LEFT_PARENTHESIS) { // function call
			ASTNode *functionCall = malloc(sizeof(ASTNode));
			functionCall->type = AST_FUNCTION_CALL;
			strcpy(functionCall->data.functionCall.functionName, tokens[i].value.identifier);
			i += 2; // +2 for the identifier and left parenthesis
			functionCall->data.functionCall.parameterCount = 0;
			while (i < tokenCount && tokens[i].type != TOKEN_TYPE_RIGHT_PARENTHESIS) {
				bool success;
				int stepForward;
				ASTNode *parameter = tryFindValueNode(tokens + i, tokenCount - i, &success, &stepForward);
				functionCall->data.functionCall.parameters[functionCall->data.functionCall.parameterCount++] = parameter;
				i += stepForward;
				if (tokens[i].type == TOKEN_TYPE_COMMA) {
					i++;
				} else if (tokens[i].type != TOKEN_TYPE_RIGHT_PARENTHESIS) {
					printf("Error: expected comma or right parenthesis\n");
					break;
				}
			}
			if (i == tokenCount) {
				printf("ERR: no matching right parenthesis\n");
				exit(1);
			}
			i++; // +1 for the right parenthesis
			current->next = functionCall;
			current = functionCall;
		} else if (tokens[i].type == TOKEN_TYPE_IDENTIFIER && tokens[i + 1].type == TOKEN_TYPE_ASSIGN) { //assignment
			ASTNode *assignment = malloc(sizeof(ASTNode));
			assignment->type = AST_ASSIGN_VARIABLE;
			strcpy(assignment->data.assignVariable.variableName, tokens[i].value.identifier);
			bool success;
			int stepForward;
			assignment->data.assignVariable.valueNode = tryFindValueNode(tokens + i + 2, tokenCount - i - 2, &success, &stepForward);
			i += stepForward + 2; // +2 for the identifier and assign token
			current->next = assignment;
			current = assignment;
		} else if (tokens[i].type == TOKEN_TYPE_IF && tokens[i + 1].type == TOKEN_TYPE_LEFT_PARENTHESIS) { // if statement
			ASTNode *ifNode = malloc(sizeof(ASTNode));
			ifNode->type = AST_IF;
			int rigtParenthesisIndex = i + 2;
			while (rigtParenthesisIndex < tokenCount && tokens[rigtParenthesisIndex].type != TOKEN_TYPE_RIGHT_PARENTHESIS) {
				rigtParenthesisIndex++;
			}
			if (rigtParenthesisIndex == tokenCount) {
				printf("ERR: no matching right parenthesis\n");
				exit(1);
			}
			bool success;
			int stepForward;
			ifNode->data.ifNode.condition = tryFindValueNode(tokens + i + 2, rigtParenthesisIndex - i - 2, &success, &stepForward);
			i += stepForward + 3;
			// +3 for the if token and left and right parenthesis
			// now i is at the first token after the right parenthesis
			ASTNode *trueBlock = tokens2AST(tokens + i, tokenCount - i, &success, &stepForward);
			ifNode->data.ifNode.trueBlock = trueBlock;
			ifNode->data.ifNode.falseBlock = NULL;
			i += stepForward;
			ASTNode *curifNode = ifNode;
			while (i < tokenCount) {
				if (tokens[i].type == TOKEN_TYPE_ELSE) {
					i++;
					ASTNode *falseBlock = tokens2AST(tokens + i, tokenCount - i, &success, &stepForward);
					curifNode->data.ifNode.falseBlock = falseBlock;
					i += stepForward;
					break;
				} else if (tokens[i].type == TOKEN_TYPE_ELSE_IF) {
					i++;
					ASTNode *elseIfNode = malloc(sizeof(ASTNode));
					elseIfNode->type = AST_IF;
					i++;
					elseIfNode->data.ifNode.condition = tryFindValueNode(tokens + i, tokenCount - i, &success, &stepForward);
					i += stepForward + 1;
					ASTNode *trueBlock = tokens2AST(tokens + i, tokenCount - i, &success, &stepForward);
					i += stepForward;
					elseIfNode->data.ifNode.trueBlock = trueBlock;
					elseIfNode->data.ifNode.falseBlock = NULL;
					curifNode->data.ifNode.falseBlock = elseIfNode;
					curifNode = elseIfNode;
				} else if (tokens[i].type == TOKEN_TYPE_END) {
					i++;
					break;
				} else {
					break;
				}
			}
			current->next = ifNode;
			current = ifNode;
		} else if (tokens[i].type == TOKEN_TYPE_WHILE) {
			ASTNode *whileNode = malloc(sizeof(ASTNode));
			whileNode->type = AST_WHILE;
			int rigtParenthesisIndex = i + 2;
			while (rigtParenthesisIndex < tokenCount && tokens[rigtParenthesisIndex].type != TOKEN_TYPE_RIGHT_PARENTHESIS) {
				rigtParenthesisIndex++;
			}
			if (rigtParenthesisIndex == tokenCount) {
				printf("ERR: no matching right parenthesis\n");
				exit(1);
			}
			bool success;
			int stepForward;
			whileNode->data.whileNode.condition = tryFindValueNode(tokens + i + 2, rigtParenthesisIndex - i - 2, &success, &stepForward);
			i += stepForward + 3; // +3 for the while token and left and right parenthesis
			// now i is at the first token after the right parenthesis
			ASTNode *body = tokens2AST(tokens + i, tokenCount - i, &success, &stepForward);
			whileNode->data.whileNode.body = body;
			i += stepForward;
			current->next = whileNode;
			current = whileNode;
		} else if (tokens[i].type == TOKEN_TYPE_RECYCLE && tokens[i + 1].type == TOKEN_TYPE_IDENTIFIER) { // recycle statement
			ASTNode *recycleNode = malloc(sizeof(ASTNode));
			recycleNode->type = AST_RECYCLE;
			strcpy(recycleNode->data.recycle.variableName, tokens[i + 1].value.identifier);
			i += 2; // +2 for the recycle token and identifier
			current->next = recycleNode;
			current = recycleNode;
		} else if (tokens[i].type == TOKEN_TYPE_CONTINUE) {
			ASTNode *continueNode = malloc(sizeof(ASTNode));
			continueNode->type = AST_CONTINUE;
			current->next = continueNode;
			current = continueNode;
			i++;
		} else if (tokens[i].type == TOKEN_TYPE_BREAK) {
			ASTNode *continueNode = malloc(sizeof(ASTNode));
			continueNode->type = AST_BREAK;
			current->next = continueNode;
			current = continueNode;
			i++;
		} else if (tokens[i].type == TOKEN_TYPE_RETURN) {
			ASTNode *continueNode = malloc(sizeof(ASTNode));
			continueNode->type = AST_RETURN;
			bool success;
			int stepForward;
			continueNode->data.returnNode.returnValue = tryFindValueNode(tokens + i + 1, tokenCount - i - 1, &success, &stepForward);
			current->next = continueNode;
			current = continueNode;
			i += stepForward + 1; // +1 for the return token
		} else if (tokens[i].type == TOKEN_TYPE_COMMA || tokens[i].type == TOKEN_TYPE_NEWLINE) {
			i++;
			continue;
		} else if (tokens[i].type == TOKEN_TYPE_END || tokens[i].type == TOKEN_TYPE_ELSE ||
				tokens[i].type == TOKEN_TYPE_ELSE_IF) {
			if (tokens[i].type == TOKEN_TYPE_END) {
				i++;
			}
			break;
		} else {
			printf("ERR: unknown token string:\n");
			printTokens(tokens + i, tokenCount - i);
			break;
		}
	}

	ASTNode *blockEnd = malloc(sizeof(ASTNode));
	blockEnd->type = AST_BLOCK_END;
	blockEnd->data.blockEnd.controlBlock = root->data.blockStart.controlBlock;
	blockEnd->data.blockEnd.startAST = root;
	root->data.blockStart.endAST = blockEnd;
	current->next = blockEnd;

	*stepForward = i;
	return root;
}

void printSingleASTNode(ASTNode *node, int indent) {
	if (node == NULL) {
		return;
	}
	char indentStr[100];
	memset(indentStr, ' ', indent);
	indentStr[indent] = '\0';
	switch (node->type) {
		case AST_FETCH_VARIABLE:
			printf("%sFetch Variable: %s\n", indentStr, node->data.fetchVariable.variableName);
			break;
		case AST_VALUE:
			if (node->data.value.type == VARIABLE_TYPE_INTEGER) {
				printf("%sValue: %d\n", indentStr, node->data.value.value.integer);
			} else if (node->data.value.type == VARIABLE_TYPE_FLOAT) {
				printf("%sValue: %f\n", indentStr, node->data.value.value.floating);
			} else {
				printf("%sUnknown Value Type\n", indentStr);
			}
			break;
		case AST_CALCULATE:
			switch (node->data.calculate.operation) {
				case TOKEN_TYPE_PLUS:
					printf("%sOperation: +\n", indentStr);
					break;
				case TOKEN_TYPE_MINUS:
					printf("%sOperation: -\n", indentStr);
					break;
				case TOKEN_TYPE_MULTIPLY:
					printf("%sOperation: *\n", indentStr);
					break;
				case TOKEN_TYPE_DIVIDE:
					printf("%sOperation: /\n", indentStr);
					break;
				case TOKEN_TYPE_LESS:
					printf("%sOperation: <\n", indentStr);
					break;
				case TOKEN_TYPE_GREATER:
					printf("%sOperation: >\n", indentStr);
					break;
				case TOKEN_TYPE_LESS_EQUAL:
					printf("%sOperation: <=\n", indentStr);
					break;
				case TOKEN_TYPE_GREATER_EQUAL:
					printf("%sOperation: >=\n", indentStr);
					break;
				case TOKEN_TYPE_EQUAL:
					printf("%sOperation: ==\n", indentStr);
					break;
				case TOKEN_TYPE_UNEQUAL:
					printf("%sOperation: !=\n", indentStr);
					break;
				case TOKEN_TYPE_AND:
					printf("%sOperation: &\n", indentStr);
					break;
				case TOKEN_TYPE_OR:
					printf("%sOperation: |\n", indentStr);
					break;
				case TOKEN_TYPE_NOT:
					printf("%sOperation: !\n", indentStr);
					break;
				default:
					printf("%sUnknown Operation\n", indentStr);
					break;
			}
			printf("%sLeft Operand: \n", indentStr);
			printASTNode(node->data.calculate.leftOperand, indent + 1);
			printf("%sRight Operand: \n", indentStr);
			printASTNode(node->data.calculate.rightOperand, indent + 1);
			break;
		case AST_ASSIGN_VARIABLE:
			printf("%sAssign Variable: %s\n", indentStr, node->data.assignVariable.variableName);
			printASTNode(node->data.assignVariable.valueNode, indent + 1);
			break;
		case AST_IF:
			printf("%sIf Statement:\n", indentStr);
			printf("%sCondition:\n", indentStr);
			printASTNode(node->data.ifNode.condition, indent + 1);
			printf("%sTrue Block:\n", indentStr);
			printASTNode(node->data.ifNode.trueBlock, indent + 1);
			if (node->data.ifNode.falseBlock != NULL) {
				printf("%sFalse Block:\n", indentStr);
				printASTNode(node->data.ifNode.falseBlock, indent + 1);
			}
			break;
		case AST_WHILE:
			printf("%sWhile Statement:\n", indentStr);
			printf("%sCondition:\n", indentStr);
			printASTNode(node->data.whileNode.condition, indent + 1);
			printf("%sBody:\n", indentStr);
			printASTNode(node->data.whileNode.body, indent + 1);
			break;
		case AST_BLOCK_START:
			printf("%sBlock Start:\n", indentStr);
			break;
		case AST_BLOCK_END:
			printf("%sBlock End:\n", indentStr);
			break;
		case AST_RECYCLE:
			printf("%sRecycle Variable: %s\n", indentStr, node->data.recycle.variableName);
			break;
		case AST_FUNCTION_ENTER:
			printf("%sFunction Enter:\n", indentStr);
			printf("%sParameter Count: %d\n", indentStr, node->data.functionEnter.parameterCount);
			for (int i = 0; i < node->data.functionEnter.parameterCount; i++) {
				printf("%sParameter %d: %s\n", indentStr, i + 1, node->data.functionEnter.parameterNames[i]);
			}
			printASTNode(node->data.functionEnter.body, indent + 1);
			break;
		case AST_FUNCTION_CALL:
			printf("%sFunction Call: %s\n", indentStr, node->data.functionCall.functionName);
			printf("%sParameter Count: %d\n", indentStr, node->data.functionCall.parameterCount);
			for (int i = 0; i < node->data.functionCall.parameterCount; i++) {
				printf("%sParameter %d: \n", indentStr, i + 1);
				printASTNode(node->data.functionCall.parameters[i], indent + 1);
			}
		case AST_CONTINUE:
			printf("%sContinue Statement:\n", indentStr);
			break;
		case AST_BREAK:
			printf("%sBreak Statement:\n", indentStr);
			break;
		case AST_RETURN:
			printf("%sReturn Statement:\n", indentStr);
			printASTNode(node->data.returnNode.returnValue, indent + 1);
			break;
		default:
			break;
	}
}

void printASTNode(ASTNode *node, int indent) {
	while (node != NULL) {
		printSingleASTNode(node, indent);
		node = node->next;
	}
}

void freeASTNode(ASTNode *node) {
	if (node == NULL) {
		return;
	}
	freeASTNode(node->next);
	switch (node->type) {
		case AST_FETCH_VARIABLE:
			break;
		case AST_VALUE:
			break;
		case AST_CALCULATE:
			freeASTNode(node->data.calculate.leftOperand);
			freeASTNode(node->data.calculate.rightOperand);
			break;
		case AST_ASSIGN_VARIABLE:
			freeASTNode(node->data.assignVariable.valueNode);
			break;
		case AST_IF:
			freeASTNode(node->data.ifNode.condition);
			freeASTNode(node->data.ifNode.trueBlock);
			freeASTNode(node->data.ifNode.falseBlock);
			break;
		case AST_WHILE:
			freeASTNode(node->data.whileNode.condition);
			freeASTNode(node->data.whileNode.body);
			break;
		case AST_BLOCK_START:
			break;
		case AST_BLOCK_END:
			break;
		case AST_RECYCLE:
			break;
		case AST_FUNCTION_ENTER:
			break;
		case AST_FUNCTION_CALL:
			for (int i = 0; i < node->data.functionCall.parameterCount; i++) {
				freeASTNode(node->data.functionCall.parameters[i]);
			}
			break;
		default:
			break;
	}
	free(node);
}

TableUnit fetchVariableValue(Table **tables, int layers, char *variableName, bool *exists) {
	for (int i = layers; i >= 0; i--) {
		printf("INFO: Fetching variable %s from layer %d\n", variableName, i);
		Table *table = tables[i];
		if (!tableExist(table, variableName)) {
			printf("INFO: Variable %s not found in layer %d\n", variableName, i);
			continue;
		}
		bool empty;
		TableUnit value = tableGet(table, variableName, &empty);
		(*exists) = !empty;
		return value;
	}
	(*exists) = false;
	return (TableUnit){ 0 };
}

Variable tableValue2Variable(TableUnit value) {
	Variable variable;
	switch (value.type) {
		case TABLE_UNIT_INTEGER:
			variable.type = VARIABLE_TYPE_INTEGER;
			variable.value.integer = value.value.integer;
			break;
		case TABLE_UNIT_FLOATING:
			variable.type = VARIABLE_TYPE_FLOAT;
			variable.value.floating = value.value.floating;
			break;
		case TABLE_UNIT_STRING:
			variable.type = VARIABLE_TYPE_STRING;
			strcpy(variable.value.string, value.value.string);
			break;
		case TABLE_UNIT_TABLE:
			variable.type = VARIABLE_TYPE_TABLE;
			variable.value.table = value.value.pointer;
			break;
		case TABLE_UNIT_FUNCTION:
			variable.type = VARIABLE_TYPE_FUNCTION;
			variable.value.function = value.value.pointer;
			break;
		case TABLE_UNIT_NULL:
			variable.type = VARIABLE_TYPE_NULL;
			break;
	}
	return variable;
}

TableUnitValue variable2tableValue(Variable value) {
	TableUnitValue tableValue;
	switch (value.type) {
		case VARIABLE_TYPE_INTEGER:
			tableValue.integer = value.value.integer;
			break;
		case VARIABLE_TYPE_FLOAT:
			tableValue.floating = value.value.floating;
			break;
		case VARIABLE_TYPE_STRING:
			strcpy(tableValue.string, value.value.string);
			break;
		case VARIABLE_TYPE_TABLE:
			tableValue.pointer = value.value.table;
			break;
		case VARIABLE_TYPE_FUNCTION:
			tableValue.pointer = value.value.function;
			break;
		case VARIABLE_TYPE_NULL:
			tableValue.pointer = NULL;
			break;
	}
	return tableValue;
}

ASTNode *parentNode = NULL;
Variable runAST(ASTNode *node, Table **globalVariables, int globalLayerCount) {
	if (node == NULL) {
		printf("INFO: ASTNode is NULL\n");
		return (Variable){ .type = VARIABLE_TYPE_NULL };
	} else if (node->type == AST_BLOCK_START) {
		printf("INFO: Block Start\n");
		globalLayerCount++;
		globalVariables[globalLayerCount] = node->data.blockStart.controlBlock->localVariables;
		return runAST(node->next, globalVariables, globalLayerCount);
	} else if (node->type == AST_BLOCK_END) {
		printf("INFO: Block End\n");
		free(node->data.blockEnd.controlBlock->localVariables);
		globalLayerCount--;
		return runAST(node->next, globalVariables, globalLayerCount);
	} else if (node->type == AST_ASSIGN_VARIABLE) {
		printf("INFO: Assign Variable: %s\n", node->data.assignVariable.variableName);
		Variable value = runAST(node->data.assignVariable.valueNode, globalVariables, globalLayerCount);
		int layer = globalLayerCount;
		while (layer >= 0) {
			if (tableExist(globalVariables[layer], node->data.assignVariable.variableName)) {
				break;
			}
			layer--;
		}
		if (layer < 0) {
            printf("INFO: Variable %s not found in any layer, creating new variable\n", node->data.assignVariable.variableName);
			layer = globalLayerCount;
		}
		TableUnitValue tableValue = variable2tableValue(value);
		if (value.type == VARIABLE_TYPE_NULL) {
			tableRemove(globalVariables[layer], node->data.assignVariable.variableName);
		} else {
			tableAdd(globalVariables[layer], node->data.assignVariable.variableName, tableValue, ASTVariableTypeToTableUnitType(value.type));
		}
		return runAST(node->next, globalVariables, globalLayerCount);
	} else if (node->type == AST_VALUE) {
		printf("INFO: Value: %d\n", node->data.value.value.integer);
		switch (node->data.value.type) {
			case VARIABLE_TYPE_INTEGER:
				return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = node->data.value.value.integer };
				break;
			case VARIABLE_TYPE_FLOAT:
				return (Variable){ .type = VARIABLE_TYPE_FLOAT, .value.floating = node->data.value.value.floating };
				break;
			case VARIABLE_TYPE_STRING:
				Variable stringVar = (Variable){ .type = VARIABLE_TYPE_STRING };
				strcpy(stringVar.value.string, node->data.value.value.string);
				return stringVar;
				break;
			case VARIABLE_TYPE_TABLE:
				Variable tableVar = (Variable){ .type = VARIABLE_TYPE_TABLE };
				tableVar.value.table = node->data.value.value.table;
				return tableVar;
				break;
			case VARIABLE_TYPE_FUNCTION:
				Variable functionVar = (Variable){ .type = VARIABLE_TYPE_FUNCTION };
				functionVar.value.function = node->data.value.value.function;
				return functionVar;
				break;
			case VARIABLE_TYPE_NULL:
				Variable nullVar = (Variable){ .type = VARIABLE_TYPE_NULL };
				return nullVar;
				break;
		}
	} else if (node->type == AST_FETCH_VARIABLE) {
		bool exists;
		TableUnit value = fetchVariableValue(globalVariables, globalLayerCount, node->data.fetchVariable.variableName, &exists);
		if (!exists) {
			return (Variable){ .type = VARIABLE_TYPE_NULL };
		}
		return tableValue2Variable(value);
	} else if (node->type == AST_CALCULATE) {
		Variable leftValue = runAST(node->data.calculate.leftOperand, globalVariables, globalLayerCount);
		Variable rightValue = { .type = VARIABLE_TYPE_INTEGER }; //fake type
		if (node->data.calculate.operation != TOKEN_TYPE_NOT) {
			rightValue = runAST(node->data.calculate.rightOperand, globalVariables, globalLayerCount);
		}
		if (leftValue.type == VARIABLE_TYPE_NULL || rightValue.type == VARIABLE_TYPE_NULL) {
			return (Variable){ .type = VARIABLE_TYPE_NULL };
		}
		switch (node->data.calculate.operation) {
			case TOKEN_TYPE_PLUS:
				if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer + rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_FLOAT, .value.floating = leftValue.value.floating + rightValue.value.floating };
				} else if (leftValue.type == VARIABLE_TYPE_STRING && rightValue.type == VARIABLE_TYPE_STRING) {
					Variable stringVar = { .type = VARIABLE_TYPE_STRING };
					strcpy(stringVar.value.string, leftValue.value.string);
					strcat(stringVar.value.string, rightValue.value.string);
					return stringVar;
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_FLOAT, .value.floating = leftValue.value.floating + rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_FLOAT, .value.floating = leftValue.value.integer + rightValue.value.floating };
				} else {
					printf("ERR: unknown type for + operation\n");
					return (Variable){ .type = VARIABLE_TYPE_NULL };
				}
				break;
			case TOKEN_TYPE_MINUS:
				if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer - rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_FLOAT, .value.floating = leftValue.value.floating - rightValue.value.floating };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_FLOAT, .value.floating = leftValue.value.floating - rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_FLOAT, .value.floating = leftValue.value.integer - rightValue.value.floating };
				} else {
					printf("ERR: unknown type for - operation\n");
					return (Variable){ .type = VARIABLE_TYPE_NULL };
				}
				break;
			case TOKEN_TYPE_MULTIPLY:
				if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer * rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_FLOAT, .value.floating = leftValue.value.floating * rightValue.value.floating };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_FLOAT, .value.floating = leftValue.value.floating * rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_FLOAT, .value.floating = leftValue.value.integer * rightValue.value.floating };
				} else {
					printf("ERR: unknown type for * operation\n");
					return (Variable){ .type = VARIABLE_TYPE_NULL };
				}
				break;
			case TOKEN_TYPE_DIVIDE:
				if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer / rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_FLOAT, .value.floating = leftValue.value.floating / rightValue.value.floating };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_FLOAT, .value.floating = leftValue.value.floating / rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_FLOAT, .value.floating = leftValue.value.integer / rightValue.value.floating };
				} else {
					printf("ERR: unknown type for / operation\n");
					return (Variable){ .type = VARIABLE_TYPE_NULL };
				}
				break;
			case TOKEN_TYPE_LESS:
				if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer < rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.floating < rightValue.value.floating };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.floating < rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer < rightValue.value.floating };
				} else {
					printf("ERR: unknown type for < operation\n");
					return (Variable){ .type = VARIABLE_TYPE_NULL };
				}
				break;
			case TOKEN_TYPE_GREATER:
				if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer > rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.floating > rightValue.value.floating };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.floating > rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer > rightValue.value.floating };
				} else {
					printf("ERR: unknown type for > operation\n");
					return (Variable){ .type = VARIABLE_TYPE_NULL };
				}
				break;
			case TOKEN_TYPE_LESS_EQUAL:
				if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer <= rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.floating <= rightValue.value.floating };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.floating <= rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer <= rightValue.value.floating };
				} else {
					printf("ERR: unknown type for <= operation\n");
					return (Variable){ .type = VARIABLE_TYPE_NULL };
				}
				break;
			case TOKEN_TYPE_GREATER_EQUAL:
				if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer >= rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.floating >= rightValue.value.floating };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.floating >= rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer >= rightValue.value.floating };
				} else {
					printf("ERR: unknown type for >= operation\n");
					return (Variable){ .type = VARIABLE_TYPE_NULL };
				}
				break;
			case TOKEN_TYPE_EQUAL:
				if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer == rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.floating == rightValue.value.floating };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.floating == rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer == rightValue.value.floating };
				} else {
					printf("ERR: unknown type for == operation\n");
					return (Variable){ .type = VARIABLE_TYPE_NULL };
				}
				break;
			case TOKEN_TYPE_UNEQUAL:
				if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer != rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.floating != rightValue.value.floating };
				} else if (leftValue.type == VARIABLE_TYPE_FLOAT && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.floating != rightValue.value.integer };
				} else if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_FLOAT) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer != rightValue.value.floating };
				} else {
					printf("ERR: unknown type for != operation\n");
					return (Variable){ .type = VARIABLE_TYPE_NULL };
				}
				break;
			case TOKEN_TYPE_AND:
				if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer & rightValue.value.integer };
				} else {
					printf("ERR: unknown type for & operation\n");
					return (Variable){ .type = VARIABLE_TYPE_NULL };
				}
				break;
			case TOKEN_TYPE_OR:
				if (leftValue.type == VARIABLE_TYPE_INTEGER && rightValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = leftValue.value.integer | rightValue.value.integer };
				} else {
					printf("ERR: unknown type for | operation\n");
					return (Variable){ .type = VARIABLE_TYPE_NULL };
				}
				break;
			case TOKEN_TYPE_NOT:
				if (leftValue.type == VARIABLE_TYPE_INTEGER) {
					return (Variable){ .type = VARIABLE_TYPE_INTEGER, .value.integer = !leftValue.value.integer };
				} else {
					printf("ERR: unknown type for ! operation\n");
					return (Variable){ .type = VARIABLE_TYPE_NULL };
				}
				break;
			default:
				printf("ERR: unknown operation\n");
		}
	} else if (node->type == AST_RECYCLE) {
		Table *table = globalVariables[globalLayerCount];
		tableRemove(table, node->data.recycle.variableName);
		return runAST(node->next, globalVariables, globalLayerCount);
	} else if (node->type == AST_IF) {
		Variable condition = runAST(node->data.ifNode.condition, globalVariables, globalLayerCount);
		if (condition.type != VARIABLE_TYPE_INTEGER) {
			printf("ERR: condition is not an integer\n");
			return runAST(node->next, globalVariables, globalLayerCount);
		}
		if (condition.value.integer) {
			return runAST(node->data.ifNode.trueBlock, globalVariables, globalLayerCount);
		} else if (node->data.ifNode.falseBlock != NULL) {
			return runAST(node->data.ifNode.falseBlock, globalVariables, globalLayerCount);
		}
		return runAST(node->next, globalVariables, globalLayerCount);
	} else if (node->type == AST_WHILE) {
		Variable condition = runAST(node->data.whileNode.condition, globalVariables, globalLayerCount);
		if (condition.type != VARIABLE_TYPE_INTEGER) {
			printf("ERR: condition is not an integer\n");
			return runAST(node->next, globalVariables, globalLayerCount);
		}
		while (condition.value.integer) {
			parentNode = node;
			runAST(node->data.whileNode.body, globalVariables, globalLayerCount);
			condition = runAST(node->data.whileNode.condition, globalVariables, globalLayerCount);
			if (condition.type != VARIABLE_TYPE_INTEGER) {
				printf("ERR: condition is not an integer\n");
				return runAST(node->next, globalVariables, globalLayerCount);
			}
		}
		return runAST(node->next, globalVariables, globalLayerCount);
	} else if (node->type == AST_BREAK) {
		if (parentNode == NULL) {
			printf("ERR: break statement outside of loop\n");
			return (Variable){ .type = VARIABLE_TYPE_NULL };
		}
		if (parentNode->type != AST_WHILE) {
			printf("ERR: break statement outside of while loop\n");
			return (Variable){ .type = VARIABLE_TYPE_NULL };
		}
		// end the current block immediately
		runAST(parentNode->data.whileNode.body->data.blockStart.endAST, globalVariables, globalLayerCount);
		return runAST(parentNode->next, globalVariables, globalLayerCount);
	} else if (node->type == AST_CONTINUE) {
		if (parentNode == NULL) {
			printf("ERR: continue statement outside of loop\n");
			return (Variable){ .type = VARIABLE_TYPE_NULL };
		}
		if (parentNode->type != AST_WHILE) {
			printf("ERR: continue statement outside of while loop\n");
			return (Variable){ .type = VARIABLE_TYPE_NULL };
		}
		// end the current block immediately
		runAST(parentNode->data.whileNode.body->data.blockStart.endAST, globalVariables, globalLayerCount);
		return runAST(parentNode, globalVariables, globalLayerCount);
	} else if (node->type == AST_FUNCTION_ENTER) {
		return (Variable){
			.type = VARIABLE_TYPE_FUNCTION,
			.value.function = node
		};
	} else if (node->type == AST_FUNCTION_CALL) {
		bool exists;
		TableUnit functionTableUnit = fetchVariableValue(globalVariables, globalLayerCount, node->data.functionCall.functionName, &exists);
		if (!exists) {
			printf("ERR: function %s not found\n", node->data.functionCall.functionName);
			return (Variable){ .type = VARIABLE_TYPE_NULL };
		}
		ASTNode *functionNode = functionTableUnit.value.pointer;
		if (functionNode == NULL) {
			printf("ERR: function %s is NULL\n", node->data.functionCall.functionName);
			return (Variable){ .type = VARIABLE_TYPE_NULL };
		}
		if (functionNode->type == AST_CALL_C_FUNCTION) {
			printf("INFOL: calling C function %s\n", node->data.functionCall.functionName);
			if (functionNode->data.callCFunction.func == NULL) {
				printf("ERR: function %s is NULL\n", node->data.functionCall.functionName);
				return (Variable){ .type = VARIABLE_TYPE_NULL };
			}
			functionNode->data.callCFunction.func(node->data.functionCall.parameters[0], globalVariables, globalLayerCount);
			return (Variable){ .type = VARIABLE_TYPE_NULL };
		}
		if (functionNode->type != AST_FUNCTION_ENTER) {
			printf("ERR: %s is not a function\n", node->data.functionCall.functionName);
			return (Variable){ .type = VARIABLE_TYPE_NULL };
		}
		if (functionNode->data.functionEnter.parameterCount != node->data.functionCall.parameterCount) {
			printf("ERR: function %s parameter count mismatch, expected %d, got %d\n", node->data.functionCall.functionName,
					functionNode->data.functionEnter.parameterCount, node->data.functionCall.parameterCount);
			return (Variable){ .type = VARIABLE_TYPE_NULL };
		}
		ASTNode *functionBlockStart = functionNode->data.functionEnter.body;
		ASTNode *cur = functionBlockStart;
		ASTNode *functionBlockStartNext = functionBlockStart->next;
		for (int i = 0; i < functionNode->data.functionEnter.parameterCount; i++) {
			// create a assignment node for each parameter
			ASTNode *assignNode = malloc(sizeof(ASTNode));
			assignNode->type = AST_ASSIGN_VARIABLE;
			assignNode->data.assignVariable.valueNode = node->data.functionCall.parameters[i];
			strcpy(assignNode->data.assignVariable.variableName, functionNode->data.functionEnter.parameterNames[i]);
			cur->next = assignNode;
			cur = assignNode;
		}
		cur->next = functionBlockStartNext;
		// run the function body
		parentNode = functionBlockStart;
		Variable returnValue = runAST(functionBlockStart, globalVariables, globalLayerCount);
		return returnValue;
	} else if (node->type == AST_RETURN) {
		Variable returnValue = runAST(node->data.returnNode.returnValue, globalVariables, globalLayerCount);
		if (parentNode == NULL) {
			printf("ERR: return statement outside of function\n");
			return (Variable){ .type = VARIABLE_TYPE_NULL };
		}
		if (parentNode->type != AST_FUNCTION_ENTER) {
			printf("ERR: return statement outside of function\n");
			return (Variable){ .type = VARIABLE_TYPE_NULL };
		}
		// end the current block immediately
		runAST(parentNode->data.functionEnter.body->data.blockStart.endAST, globalVariables, globalLayerCount);
		return returnValue;
	} else {
		printf("ERR: unknown AST node type\n");
		return (Variable){ .type = VARIABLE_TYPE_NULL };
	}
	return (Variable){ .type = VARIABLE_TYPE_NULL };
}

void printVariable(Variable var) {
	switch (var.type) {
		case VARIABLE_TYPE_INTEGER:
			printf("Integer: %d\n", var.value.integer);
			break;
		case VARIABLE_TYPE_FLOAT:
			printf("Float: %f\n", var.value.floating);
			break;
		case VARIABLE_TYPE_STRING:
			printf("String: %s\n", var.value.string);
			break;
		case VARIABLE_TYPE_TABLE:
			printf("Table: %p\n", (void *)var.value.table);
			break;
		case VARIABLE_TYPE_FUNCTION:
			printf("Function: %p\n", var.value.function);
			break;
		case VARIABLE_TYPE_NULL:
			printf("Null\n");
			break;
		default:
			printf("Unknown Variable Type\n");
			break;
	}
}
