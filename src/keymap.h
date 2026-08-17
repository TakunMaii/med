#ifndef MED_KEYMAP_H
#define MED_KEYMAP_H

#include "med.h"

typedef struct {
    Mode mode;
    char waiting_char;
    char operator_pending;
    char window_pending;
} KeymapContext;

typedef struct {
    int key;
    int mods_required;
    int mods_forbidden;
    bool use_char;
    char ch;
    bool mode_exact;
    Mode mode;
    bool waiting_char_exact;
    char waiting_char;
    bool operator_pending_exact;
    char operator_pending;
    bool window_pending_exact;
    char window_pending;
    int priority;
} KeymapBinding;

typedef struct {
    int key;
    int mods;
    char ch;
    KeymapContext ctx;
} KeymapProbe;

size_t keymap_resolve(const KeymapBinding *bindings, size_t count, const KeymapProbe *probe, int *score_out);

#endif
