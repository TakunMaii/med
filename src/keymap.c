#include "keymap.h"

static int keymap_score_binding(const KeymapBinding *b, const KeymapProbe *p) {
    if (b->key >= 0 && b->key != p->key) return -1;
    if ((p->mods & b->mods_required) != b->mods_required) return -1;
    if (p->mods & b->mods_forbidden) return -1;
    if (b->use_char && b->ch != p->ch) return -1;
    if (b->mode_exact && b->mode != p->ctx.mode) return -1;
    if (b->waiting_char_exact && b->waiting_char != p->ctx.waiting_char) return -1;
    if (b->operator_pending_exact && b->operator_pending != p->ctx.operator_pending) return -1;
    if (b->window_pending_exact && b->window_pending != p->ctx.window_pending) return -1;

    int score = b->priority * 64;
    if (b->key >= 0) score += 32;
    if (b->mods_required || b->mods_forbidden) score += 16;
    if (b->use_char) score += 24;
    if (b->mode_exact) score += 8;
    if (b->waiting_char_exact) score += 8;
    if (b->operator_pending_exact) score += 8;
    if (b->window_pending_exact) score += 8;
    return score;
}

size_t keymap_resolve(const KeymapBinding *bindings, size_t count, const KeymapProbe *probe, int *score_out) {
    size_t best = SIZE_MAX;
    int best_score = -1;
    for (size_t i = 0; i < count; i++) {
        int score = keymap_score_binding(&bindings[i], probe);
        if (score < 0) continue;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    if (score_out) *score_out = best_score;
    return best;
}
