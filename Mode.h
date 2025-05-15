#ifndef MODE_H
#define MODE_H

enum Mode {
    MODE_NORMAL = 1,
    MODE_INSERT = 1<<1,
    MODE_VISUAL = 1<<2,
    MODE_VISUAL_LINE = 1<<3,
    MODE_VISUAL_BLOCK = 1<<4,
};

#endif
