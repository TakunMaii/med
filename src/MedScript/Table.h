#ifndef TABLE_H
#define TABLE_H

#define TABLE_MAX_COUNT 1024

#include <stdbool.h>

typedef union{
    long integer;
    double floating;
    char string[256];
    void *pointer;
} TableUnitValue;

typedef enum{
    TABLE_UNIT_INTEGER,
    TABLE_UNIT_FLOATING,
    TABLE_UNIT_STRING,
    TABLE_UNIT_FUNCTION,
    TABLE_UNIT_TABLE,
    TABLE_UNIT_NULL,
} TableUnitType;

typedef struct {
    char key[64];
    TableUnitValue value;
    TableUnitType type;
} TableUnit;

typedef struct {
    TableUnit *content;
    int count;
    int max_count;
} Table;

Table* tableCreate(void);

void tableFree(Table *table);

void tableAdd(Table *table, char *key, TableUnitValue value, TableUnitType type);

TableUnit tableGet(Table *table, char *key, bool *exists);

void tableRemove(Table *table, char *key);

bool tableExist(Table *table, char *key);

#endif
