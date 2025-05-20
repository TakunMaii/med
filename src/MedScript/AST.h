#ifndef AST_H
#define AST_H

#include "Table.h"
#include "Token.h"
#include <stdbool.h>

typedef enum{
    VARIABLE_TYPE_STRING,
    VARIABLE_TYPE_INTEGER,
    VARIABLE_TYPE_FLOAT,
    VARIABLE_TYPE_FUNCTION,
    VARIABLE_TYPE_TABLE,
    VARIABLE_TYPE_NULL,
} VariableType;

typedef union{
    char string[256];
    int integer;
    float floating;
    void* function;
    Table *table;
} VariableValue;

typedef struct{
    VariableType type;
    VariableValue value;
} Variable;

typedef struct{
    Table* localVariables;
} CodeBlockControlBlock;

typedef enum{
    AST_ASSIGN_VARIABLE,
    AST_VALUE,
    AST_FETCH_VARIABLE,
    AST_CALCULATE,
    AST_BLOCK_START,
    AST_BLOCK_END,
    AST_IF,
    AST_WHILE,
    AST_CONTINUE,
    AST_BREAK,
    AST_RETURN,
    AST_RECYCLE,
    AST_FUNCTION_ENTER,
    AST_FUNCTION_CALL,
    AST_CALL_C_FUNCTION,
} ASTNodeType;

typedef struct ASTNode {
    struct ASTNode* next;
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
        // Block start
        struct {
            CodeBlockControlBlock* controlBlock;
            struct ASTNode* endAST;
        } blockStart;
        // Block end
        struct {
            CodeBlockControlBlock* controlBlock;
            struct ASTNode* startAST;
        } blockEnd;
        //if
        struct {
            struct ASTNode* condition;
            struct ASTNode* trueBlock;
            struct ASTNode* falseBlock;
        } ifNode;
        // while
        struct {
            struct ASTNode* condition;
            struct ASTNode* body;
        } whileNode;
        // recycle
        struct {
            char variableName[64];
        } recycle;
        // function enter
        struct {
            int parameterCount;
            char parameterNames[16][64];
            struct ASTNode* body;
        } functionEnter;
        // function call
        struct {
            int parameterCount;
            char functionName[64];
            struct ASTNode* parameters[16];
        } functionCall;
        //continue or break
        struct{
            struct ASTNode* body;
        } continueOrBreak;
        // return
        struct{
            struct ASTNode* returnValue;
        } returnNode;
        //call c function
        struct {
            void (*func)(struct ASTNode* parameter, Table **globalVariables, int globalLayerCount);
        } callCFunction;
    } data;
    ASTNodeType type;
} ASTNode;

ASTNode* tokens2AST(Token* tokens, int tokenCount, bool *success, int *stepForward);
void printASTNode(ASTNode *node, int indent);
Variable runAST(ASTNode* node, Table **globalVariables, int globalLayerCount);
void freeASTNode(ASTNode *node);
TableUnitType ASTVariableTypeToTableUnitType(VariableType type);
VariableType TableUnitTypeToASTVariableType(TableUnitType type);
void printVariable(Variable var);

#endif
