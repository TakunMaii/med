#include "Table.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TableUnit *tableFindUnit(Table *table, char *key, bool *empty);

int hash(char *str) {
	int hash = 0;
	int p = 0;
	while (str[p] != '\0') {
		hash = (hash << 5) + str[p];
	}
	return hash;
}

void tableResize(Table *table, int new_max_count) {
	TableUnit *new_content = malloc(sizeof(TableUnit) * new_max_count);
	for (int i = 0; i < new_max_count; i++) {
		new_content[i].key[0] = '\0';
	}
	for (int i = 0; i < table->max_count; i++) {
		if (table->content[i].key[0] == '\0') {
			continue;
		}
		bool empty;
		TableUnit *unit = tableFindUnit(table, table->content[i].key, &empty);
		strcpy(unit->key, table->content[i].key);
		unit->value = table->content[i].value;
	}
	free(table->content);
	table->content = new_content;
	table->max_count = new_max_count;
}

Table *tableCreate(void) {
	Table *table = malloc(sizeof(Table));
	table->max_count = 17;
	table->count = 0;
    table->content = malloc(sizeof(TableUnit) * table->max_count);
	for (int i = 0; i < table->max_count; i++) {
		table->content[i].key[0] = '\0';
	}
	return table;
}

void tableFree(Table *table) {
	free(table->content);
	free(table);
}

TableUnit *tableFindUnit(Table *table, char *key, bool *empty) {
	int index = hash(key) % table->max_count;
	int counter = 0;
	while (table->content[index].key[0] != '\0') {
		if (strcmp(table->content[index].key, key) == 0) {
			*empty = false;
			return &table->content[index];
		}
		index = (index + 1) % table->max_count;
		counter++;
		if (counter > table->max_count) {
			counter = 0;
			tableResize(table, table->max_count * 2 + 1);
			index = hash(key) % table->max_count;
		}
	}
	*empty = true;
	return &table->content[index];
}

void tableAdd(Table *table, char *key, TableUnitValue value) {
    bool empty;
    TableUnit *unit = tableFindUnit(table, key, &empty);
    if (empty) {
        table->count++;
    }else {
        printf("INFO: (In tableAdd) Key %s already exists, updating value.\n", key);
    }
    strcpy(unit->key, key);
    unit->value = value;
}

TableUnitValue tableGet(Table *table, char *key)
{
    bool empty;
    TableUnit *unit = tableFindUnit(table, key, &empty);
    if (empty) {
        printf("ERROR: (In tableGet) Key %s not found, returned value will be unpredictable.\n", key);
    }
    return unit->value;
}

void tableRemove(Table *table, char *key)
{
    bool empty;
    TableUnit *unit = tableFindUnit(table, key, &empty);
    if (empty) {
        printf("ERROR: (In tableRemove) Key %s not found.\n", key);
        return;
    }
    unit->key[0] = '\0';
    table->count--;
}
