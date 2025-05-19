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
		functionEnter->next = tokens2AST(tokens + i, tokenCount - i, &localSuccess, &localStepForward);
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
			functionEnter->next = tokens2AST(tokens + i, tokenCount - i, &success, &stepForward);
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
	current->next = blockEnd;

	*stepForward = i;
	return root;
}

void printSingleASTNode(ASTNode *node) {
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
				case TOKEN_TYPE_LESS:
					printf("Operation: <\n");
					break;
				case TOKEN_TYPE_GREATER:
					printf("Operation: >\n");
					break;
				case TOKEN_TYPE_LESS_EQUAL:
					printf("Operation: <=\n");
					break;
				case TOKEN_TYPE_GREATER_EQUAL:
					printf("Operation: >=\n");
					break;
				case TOKEN_TYPE_EQUAL:
					printf("Operation: ==\n");
					break;
				case TOKEN_TYPE_UNEQUAL:
					printf("Operation: !=\n");
					break;
				case TOKEN_TYPE_AND:
					printf("Operation: &\n");
					break;
				case TOKEN_TYPE_OR:
					printf("Operation: |\n");
					break;
				case TOKEN_TYPE_NOT:
					printf("Operation: !\n");
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
		case AST_IF:
			printf("If Statement:\n");
			printf("Condition:\n");
			printASTNode(node->data.ifNode.condition);
			printf("True Block:\n");
			printASTNode(node->data.ifNode.trueBlock);
			if (node->data.ifNode.falseBlock != NULL) {
				printf("False Block:\n");
				printASTNode(node->data.ifNode.falseBlock);
			}
			break;
		case AST_WHILE:
			printf("While Statement:\n");
			printf("Condition:\n");
			printASTNode(node->data.whileNode.condition);
			printf("Body:\n");
			printASTNode(node->data.whileNode.body);
			break;
		case AST_BLOCK_START:
			printf("Block Start:\n");
			break;
		case AST_BLOCK_END:
			printf("Block End:\n");
			break;
		case AST_RECYCLE:
			printf("Recycle Variable: %s\n", node->data.recycle.variableName);
			break;
		case AST_FUNCTION_ENTER:
			printf("Function Enter:\n");
			printf("Parameter Count: %d\n", node->data.functionEnter.parameterCount);
			for (int i = 0; i < node->data.functionEnter.parameterCount; i++) {
				printf("Parameter %d: %s\n", i + 1, node->data.functionEnter.parameterNames[i]);
			}
			break;
		case AST_FUNCTION_CALL:
			printf("Function Call: %s\n", node->data.functionCall.functionName);
			printf("Parameter Count: %d\n", node->data.functionCall.parameterCount);
			for (int i = 0; i < node->data.functionCall.parameterCount; i++) {
				printf("Parameter %d: \n", i + 1);
				printASTNode(node->data.functionCall.parameters[i]);
			}
		default:
			break;
	}
}

void printASTNode(ASTNode *node) {
	while (node != NULL) {
		printSingleASTNode(node);
		node = node->next;
	}
}
