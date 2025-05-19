#ifndef TABLE_H
#define TABLE_H

#define TABLE_MAX_COUNT 1024

typedef union{
    long integer;
    double floating;
    void *pointer;
} TableUnitValue;

typedef struct {
    char key[64];
    TableUnitValue value;
} TableUnit;

typedef struct {
    TableUnit *content;
    int count;
    int max_count;
} Table;

Table* tableCreate(void);

void tableFree(Table *table);

void tableAdd(Table *table, char *key, TableUnitValue value);

TableUnitValue tableGet(Table *table, char *key);

void tableRemove(Table *table, char *key);

#endif
