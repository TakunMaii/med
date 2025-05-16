#include "Mode.h"

const char* modeToString(enum Mode mode) {
    switch (mode) {
        case MODE_NORMAL: return "-- NORMAL --";
        case MODE_INSERT: return "-- INSERT --";
        case MODE_VISUAL: return "-- VISUAL --";
        case MODE_VISUAL_LINE: return "-- VISUAL LINE --";
        case MODE_VISUAL_BLOCK: return "-- VISUAL BLOCK --";
        default: return "-- UNKNOWN MODE --";
    }
}
