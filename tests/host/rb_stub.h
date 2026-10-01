/* rb_stub.h - what tests/host/rb_stub.c measured since rb_stub_frame(). */
#ifndef RB_STUB_H
#define RB_STUB_H
#include <stdint.h>

typedef struct {
    uint64_t hash;              /* FNV-1a of every call's inputs */
    long draws, tris, tris_shown, binds, states, matrices;
    double fill, fill_blend;    /* pixels covered (clears included), and of them blended */
} rb_stub_stats;

extern rb_stub_stats rb_stub;
void rb_stub_frame(void);       /* reset the counters and the hash */
void rb_stub_size(int w, int h);

#endif
