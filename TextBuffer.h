#ifndef TEXT_BUFFER_H
#define TEXT_BUFFER_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TEXT_BUFFER_MAX_LINES 256
#define TEXT_BUFFER_MAX_LINE_LENGTH 256

typedef struct {
    char *content;
} LineBuffer;

typedef struct {
    LineBuffer **lines;
    int line_count;
} TextBuffer;

LineBuffer *createLineBuffer();

LineBuffer *createLineBufferWith(const char* content);

TextBuffer *createTextBuffer();

TextBuffer *createTextBufferWith(char* content);

TextBuffer* createTextBufferWithFile(const char* filename);

void releaseLineBuffer(LineBuffer *line);

void releaseTextBuffer(TextBuffer *textBuffer);

void insertNewLineAt(TextBuffer *textBuffer, int lineIndex,const char* content);

void insertCharAt(TextBuffer *textBuffer, int lineIndex, int charIndex, char content);

void deleteCharAt(TextBuffer *textBuffer, int lineIndex, int charIndex);

char *wholeText(TextBuffer *textBuffer);

void deleteLineAt(TextBuffer *textBuffer, int lineIndex);

#endif// TEXT_BUFFER_H
