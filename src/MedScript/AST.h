#ifndef AST_H
#define AST_H

#include "Table.h"
#include "Token.h"

typedef enum{
    VARIABLE_TYPE_STRING,
    VARIABLE_TYPE_INTEGER,
    VARIABLE_TYPE_FLOAT,
    VARIABLE_TYPE_FUNCTION,
    VARIABLE_TYPE_TABLE,
} VariableType;

typedef union{
    char string[256];
    int integer;
    float floating;
    void* function;
    Table table;
} VariableValue;

typedef enum{
    AST_ASSIGN_VARIABLE,
    AST_VALUE,
    AST_FETCH_VARIABLE,
    AST_CALCULATE,
} ASTNodeType;

typedef struct ASTNode {
    struct ASTNode* nextNode;
    union {
        // Variable assignment
        struct{
            char variableName[64];
            struct ASTNode* valueNode;
        } assignVariable;
        // Value node
        struct{
            VariableType type;
            VariableValue value;
        } value;
        // Variable fetch
        struct{
            char variableName[64];
        } fetchVariable;
        // Calculation
        struct {
            TokenType operation;            
            struct ASTNode* leftOperand;
            struct ASTNode* rightOperand;
        } calculate;
    } data;
    ASTNodeType type;
} ASTNode;

ASTNode* tokens2AST(Token* tokens, int tokenCount);
void printASTNode(ASTNode *node);

#endif
