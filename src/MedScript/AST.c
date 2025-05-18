#include "AST.h"
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

ASTNodeStack *createASTNodeStack() {
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

// ------------

// Shunting Yard Algorithm
// reference: https://cloud.tencent.com/developer/article/1607240
// TODO: problem to be fixed: consider the tokens are : 2 2 + + 2
ASTNode *tryFindValueNode(Token *tokens, int tokenCount, bool *success, int *stepForward) {
	*stepForward = 0;
	*success = false;

	ASTNodeStack *valueStack = createASTNodeStack();
	ASTNodeStack *operatorStack = createASTNodeStack();

	while (*stepForward < tokenCount) {
		Token token = tokens[*stepForward];
		if (token.type == TOKEN_TYPE_IDENTIFIER) {
			ASTNode *valueNode = malloc(sizeof(ASTNode));
			valueNode->type = AST_VALUE;
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
		} else if (token.type == TOKEN_TYPE_PLUS) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_PLUS;

			if (operatorStack->ptr > 0) {
				ASTNode *topOperator = popASTNode(operatorStack);
				if (topOperator->data.calculate.operation == TOKEN_TYPE_DIVIDE || topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY) {
					ASTNode *value1 = popASTNode(valueStack);
					ASTNode *value2 = popASTNode(valueStack);
					ASTNode *calculatedValue = malloc(sizeof(ASTNode));
					calculatedValue->type = AST_VALUE;
					calculatedValue->data.value.type = (value1->data.value.type == VARIABLE_TYPE_INTEGER && value2->data.value.type == VARIABLE_TYPE_INTEGER) ? VARIABLE_TYPE_INTEGER : VARIABLE_TYPE_FLOAT;
					if (calculatedValue->data.value.type == VARIABLE_TYPE_INTEGER) {
						if (topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY) {
							calculatedValue->data.value.value.integer = value1->data.value.value.integer * value2->data.value.value.integer;
						} else {
							calculatedValue->data.value.value.integer = value2->data.value.value.integer / value1->data.value.value.integer;
						}
					} else {
						if (topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY) {
							calculatedValue->data.value.value.floating = value1->data.value.value.floating * value2->data.value.value.floating;
						} else {
							calculatedValue->data.value.value.floating = value2->data.value.value.floating / value1->data.value.value.floating;
						}
					}
                    pushASTNode(valueStack, calculatedValue);
                    free(value1);
                    free(value2);
                    free(topOperator);
				}
			}

            pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_MINUS) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_MINUS;

			if (operatorStack->ptr > 0) {
				ASTNode *topOperator = popASTNode(operatorStack);
				if (topOperator->data.calculate.operation == TOKEN_TYPE_DIVIDE || topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY) {
					ASTNode *value1 = popASTNode(valueStack);
					ASTNode *value2 = popASTNode(valueStack);
					ASTNode *calculatedValue = malloc(sizeof(ASTNode));
					calculatedValue->type = AST_VALUE;
					calculatedValue->data.value.type = (value1->data.value.type == VARIABLE_TYPE_INTEGER && value2->data.value.type == VARIABLE_TYPE_INTEGER) ? VARIABLE_TYPE_INTEGER : VARIABLE_TYPE_FLOAT;
					if (calculatedValue->data.value.type == VARIABLE_TYPE_INTEGER) {
						if (topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY) {
							calculatedValue->data.value.value.integer = value1->data.value.value.integer * value2->data.value.value.integer;
						} else {
							calculatedValue->data.value.value.integer = value2->data.value.value.integer / value1->data.value.value.integer;
						}
					} else {
						if (topOperator->data.calculate.operation == TOKEN_TYPE_MULTIPLY) {
							calculatedValue->data.value.value.floating = value1->data.value.value.floating * value2->data.value.value.floating;
						} else {
							calculatedValue->data.value.value.floating = value2->data.value.value.floating / value1->data.value.value.floating;
						}
					}
                    pushASTNode(valueStack, calculatedValue);
                    free(value1);
                    free(value2);
                    free(topOperator);
				}
			}

            pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_MULTIPLY) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_MULTIPLY;
            pushASTNode(operatorStack, node);
			(*stepForward)++;
		} else if (token.type == TOKEN_TYPE_DIVIDE) {
			ASTNode *node = malloc(sizeof(ASTNode));
			node->type = AST_CALCULATE;
			node->data.calculate.operation = TOKEN_TYPE_DIVIDE;
            pushASTNode(operatorStack, node);
			(*stepForward)++;
		}
	}

    while(operatorStack->ptr > 0) {
        ASTNode *topOperator = popASTNode(operatorStack);
        ASTNode *value1 = popASTNode(valueStack);
        ASTNode *value2 = popASTNode(valueStack);
        topOperator->data.calculate.leftOperand = value2;
        topOperator->data.calculate.rightOperand = value1;
        pushASTNode(valueStack, topOperator);
        free(value1);
        free(value2);
    }

    *success = true;
    if(valueStack->ptr != 1)
    {
        printf("ERR: ASTNodeStack valueStack size is not 1\n");
        exit(1);
    }
	return popASTNode(valueStack);
}

ASTNode *tokens2AST(Token *tokens, int tokenCount) {
	ASTNode *root = NULL;
	int i = 0;
	while (i < tokenCount) {
		if (tokens[i].type == TOKEN_TYPE_IDENTIFIER && tokens[i + 1].type == TOKEN_TYPE_ASSIGN) { //assignment
			ASTNode *assignment = malloc(sizeof(ASTNode));
			assignment->type = AST_ASSIGN_VARIABLE;
			strcpy(assignment->data.assignVariable.variableName, tokens[i].value.identifier);
			assignment->data.assignVariable.valueNode = NULL;
		}
	}

}
