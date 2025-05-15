#include "Mode.h"

enum Mode theMode = MODE_NORMAL;

void switchModeTo(enum Mode newMode) {
    theMode = newMode;
}
