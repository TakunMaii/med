#include "TextBuffer.h"

LineBuffer *createLineBuffer()
{
    LineBuffer *line = (LineBuffer *)malloc(sizeof(LineBuffer));
    line->content = (char *)malloc(TEXT_BUFFER_MAX_LINE_LENGTH);
    return line;
}

LineBuffer *createLineBufferWith(const char* content)
{
    LineBuffer *line = (LineBuffer *)malloc(sizeof(LineBuffer));
    line->content = (char *)malloc(TEXT_BUFFER_MAX_LINE_LENGTH);
    memcpy(line->content, content, strlen(content));
    line->content[strlen(content)] = '\0'; // Null-terminate the string
    return line;
}

TextBuffer *createTextBuffer()
{
    TextBuffer *textBuffer = (TextBuffer *)malloc(sizeof(TextBuffer));
    textBuffer->lines = (LineBuffer **)malloc(TEXT_BUFFER_MAX_LINES * sizeof(LineBuffer*));
    textBuffer->line_count = 0;
    return textBuffer;
}

TextBuffer *createTextBufferWith(char* content)
{
    TextBuffer *textBuffer = (TextBuffer *)malloc(sizeof(TextBuffer));
    textBuffer->lines = (LineBuffer **)malloc(TEXT_BUFFER_MAX_LINES * sizeof(LineBuffer*));
    textBuffer->line_count = 0;

    if(strlen(content) == 0)
    {
        textBuffer->lines[0] = createLineBufferWith("");
        textBuffer->line_count = 1;
        return textBuffer;
    }

    char *linecpy = (char *)malloc(strlen(content) + 1);
    strcpy(linecpy, content);
    char *line = strtok(linecpy, "\n");
    printf("INFO: Creating text buffer with line: %s\n", line);
    while (line != NULL && textBuffer->line_count < TEXT_BUFFER_MAX_LINES) {
        LineBuffer *newLine = createLineBufferWith(line);
        textBuffer->lines[textBuffer->line_count++] = newLine;
        line = strtok(NULL, "\n");
        if(line != NULL)
            printf("INFO: Creating text buffer with line: %s\n", line);
        else
            printf("INFO: Text Buffer Creation Completion\n");
    }
    return textBuffer;
}

TextBuffer* createTextBufferWithFile(const char* filename)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        printf("ERR: Could not open file %s\n", filename);
        return NULL;
    }
    TextBuffer *textBuffer = createTextBuffer();
    char line[TEXT_BUFFER_MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), file) != NULL && textBuffer->line_count < TEXT_BUFFER_MAX_LINES) {
        LineBuffer *newLine = createLineBufferWith(line);
        textBuffer->lines[textBuffer->line_count++] = newLine;
    }
    fclose(file);
    return textBuffer;
}

void releaseLineBuffer(LineBuffer *line)
{
    if (line) {
        free(line->content);
        free(line);
    }
}

void releaseTextBuffer(TextBuffer *textBuffer)
{
    if (textBuffer) {
        for (int i = 0; i < textBuffer->line_count; ++i) {
            releaseLineBuffer(textBuffer->lines[i]);
        }
        free(textBuffer->lines);
        free(textBuffer);
    }
}

void insertNewLineAt(TextBuffer *textBuffer, int lineIndex,const char* content)
{
    if(textBuffer->line_count >= TEXT_BUFFER_MAX_LINES)
    {
        printf("ERR: Text Buffer is full but still trying to insert a new line\n");
        return; // Buffer is full
    }

    if(lineIndex > textBuffer->line_count)
    {
        lineIndex = textBuffer->line_count;
    }
    LineBuffer *newLine = createLineBufferWith(content);
    if (lineIndex < textBuffer->line_count) {
        memmove(&textBuffer->lines[lineIndex + 1], &textBuffer->lines[lineIndex], 
                (textBuffer->line_count - lineIndex) * sizeof(LineBuffer*));
    }
    textBuffer->lines[lineIndex] = newLine;
    textBuffer->line_count++;
}

void insertCharAt(TextBuffer *textBuffer, int lineIndex, int charIndex, char content)
{
    if(lineIndex >= textBuffer->line_count)
    {
        printf("ERR: Line index out of bounds\n");
        return; // Line index out of bounds
    }

    if(charIndex > strlen(textBuffer->lines[lineIndex]->content))
    {
        charIndex = strlen(textBuffer->lines[lineIndex]->content);
    }

    if(strlen(textBuffer->lines[lineIndex]->content) >= TEXT_BUFFER_MAX_LINE_LENGTH)
    {
        printf("ERR: Line is full but still trying to insert a new char\n");
        return; // Line is full
    }

    LineBuffer *line = textBuffer->lines[lineIndex];
    memmove(&line->content[charIndex + 1], &line->content[charIndex], 
            strlen(line->content) - charIndex + 1);// +1 for null terminator
    line->content[charIndex] = content;
}

void deleteCharAt(TextBuffer *textBuffer, int lineIndex, int charIndex)
{
    if(lineIndex >= textBuffer->line_count)
    {
        printf("WARN: Line index out of bounds\n");
        return; // Line index out of bounds
    }

    if(lineIndex < 0)
    {
        printf("WARN: Line index out of bounds\n");
        return; // Line index out of bounds
    }

    if(charIndex >= strlen(textBuffer->lines[lineIndex]->content))
    {
        printf("WARN: Char index out of bounds\n");
        return; // Char index out of bounds
    }

    if(charIndex < 0)
    {
        printf("WARN: Char index out of bounds\n");
        return; // Char index out of bounds
    }

    LineBuffer *line = textBuffer->lines[lineIndex];
    memmove(&line->content[charIndex], &line->content[charIndex + 1], 
            strlen(line->content) - charIndex);
}

char *wholeText(TextBuffer *textBuffer)
{
    int totalLength = 0;
    for (int i = 0; i < textBuffer->line_count; ++i) {
        totalLength += strlen(textBuffer->lines[i]->content) + 1; // +1 for newline
    }

    char *text = (char *)malloc(totalLength);
    int offset = 0;
    for (int i = 0; i < textBuffer->line_count; ++i) {
        memcpy(text + offset, textBuffer->lines[i]->content, strlen(textBuffer->lines[i]->content));
        offset += strlen(textBuffer->lines[i]->content);
        text[offset++] = '\n';
    }
    text[offset - 1] = '\0'; // Replace last newline with null terminator
    return text; 
}

void deleteLineAt(TextBuffer *textBuffer, int lineIndex)
{
    if(lineIndex >= textBuffer->line_count)
    {
        printf("ERR: Line index out of bounds\n");
        return; // Line index out of bounds
    }

    if(lineIndex < 0)
    {
        printf("ERR: Line index out of bounds\n");
        return; // Line index out of bounds
    }

    LineBuffer *line = textBuffer->lines[lineIndex];
    memmove(&textBuffer->lines[lineIndex], &textBuffer->lines[lineIndex + 1], 
            (textBuffer->line_count - lineIndex - 1) * sizeof(LineBuffer*));
    textBuffer->line_count--;
    releaseLineBuffer(line);
}

