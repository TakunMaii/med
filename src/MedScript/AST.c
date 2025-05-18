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
        } else {
            // encounter an non-calculation token, which means
            // here is the end of the expression
            break;
        }
	}

    printf("INFO: ASTNodeStack valueStack size is %d\n", valueStack->ptr);
    printf("INFO: ASTNodeStack operatorStack size is %d\n", operatorStack->ptr);
    while(operatorStack->ptr > 0) {
        ASTNode *topOperator = popASTNode(operatorStack);
        ASTNode *value1 = popASTNode(valueStack);
        ASTNode *value2 = popASTNode(valueStack);
        topOperator->data.calculate.leftOperand = value2;
        topOperator->data.calculate.rightOperand = value1;
        pushASTNode(valueStack, topOperator);
    }

    *success = true;
    if(valueStack->ptr != 1)
    {
        printf("WARN: ASTNodeStack valueStack size is not 1\n");
    }
	return popASTNode(valueStack);
}

ASTNode *tokens2AST(Token *tokens, int tokenCount) {
	ASTNode *root = NULL;
    bool success;
	int i = 0;
    return tryFindValueNode(tokens, tokenCount, &success, &i);
    // test
	while (i < tokenCount) {
		if (tokens[i].type == TOKEN_TYPE_IDENTIFIER && tokens[i + 1].type == TOKEN_TYPE_ASSIGN) { //assignment
			ASTNode *assignment = malloc(sizeof(ASTNode));
			assignment->type = AST_ASSIGN_VARIABLE;
			strcpy(assignment->data.assignVariable.variableName, tokens[i].value.identifier);
			assignment->data.assignVariable.valueNode = NULL;
		}
	}

}

void printASTNode(ASTNode *node) {
    if (node == NULL) {
        return;
    }
    switch (node->type) {
    case AST_FETCH_VARIABLE:
        printf("Fetch Variable: %s\n", node->data.fetchVariable.variableName);
        break;
    case AST_VALUE:
        if (node->data.value.type == VARIABLE_TYPE_INTEGER) {
            printf("Value: %d\n", node->data.value.value.integer);
        } else if (node->data.value.type == VARIABLE_TYPE_FLOAT) {
            printf("Value: %f\n", node->data.value.value.floating);
        } else {
            printf("Unknown Value Type\n");
        }
        break;
    case AST_CALCULATE:
        switch (node->data.calculate.operation) {
        case TOKEN_TYPE_PLUS:
            printf("Operation: +\n");
            break;
        case TOKEN_TYPE_MINUS:
            printf("Operation: -\n");
            break;
        case TOKEN_TYPE_MULTIPLY:
            printf("Operation: *\n");
            break;
        case TOKEN_TYPE_DIVIDE:
            printf("Operation: /\n");
            break;
        default:
            printf("Unknown Operation\n");
            break;
        }
        printf("Left Operand: \n");
        printASTNode(node->data.calculate.leftOperand);
        printf("Right Operand: \n");
        printASTNode(node->data.calculate.rightOperand);
        break;
    case AST_ASSIGN_VARIABLE:
        printf("Assign Variable: %s\n", node->data.assignVariable.variableName);
        printASTNode(node->data.assignVariable.valueNode);
        break;
    default:
        break;
    }
}
