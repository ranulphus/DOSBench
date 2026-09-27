/* model.c - model scenes: a camera orbiting one model (lighting baked into
 * vertex colours by tools/assets.py), static meshes, depth-tested, back
 * faces culled unless the model is marked two-sided, textures modulated,
 * bilinear and mipmapped. */
#include "bench.h"
#include "scene.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int model_setup(tctx *t)
{
    scene *s = (scene *)calloc(1, sizeof *s);
    char err[96];
    t->p = s;
    if (!s)
        return -1;
    if (sc_load(s, db_path(t->def->file), err, sizeof err) < 0) {
        char *c;
        snprintf(t->note, sizeof t->note, "why=%.55s", err);
        for (c = t->note; *c; c++)
            if (*c == ' ')
                *c = '_';
        return 1;                       /* no data: skipped, not failed */
    }
    if (sc_upload(s, 1) < 0) {
        strcpy(t->note, "why=upload-failed");
        return -1;
    }
    t->tris = s->tris;
    t->capture_frame = (int)s->view.frames / 8;
    snprintf(t->note, sizeof t->note, "verts=%ld textures=%d", s->nvert, s->ntex);
    return 0;
}

static void model_frame(tctx *t, int f)
{
    scene *s = (scene *)t->p;
    mat4 p, mv;
    float eye[3], at[3], up[3] = { 0, 1, 0 };
    rb_state st;
    int i;
    rb_clear(0x303848);
    db_projection(&p, DB_FOVY, s->view.znear, s->view.zfar);
    sc_camera(s, f, eye, at);
    m4_lookat(&mv, eye, at, up);
    rb_set_matrices(&p, &mv);
    db_state_default(&st);
    st.bilinear = 1;
    for (i = 0; i < s->nbatch; i++) {
        const sc_batch *b = &s->batch[i];
        st.tex = b->tex != SC_NONE ? RB_TEX_MODULATE : RB_TEX_OFF;
        st.cull = !(b->flags & SC_BF_TWOSIDED);
        rb_set_state(&st);
        rb_tex_bind(b->tex != SC_NONE ? s->rtex[b->tex] : NULL);
        rb_mesh_draw(s->mesh[i]);
    }
    rb_tex_bind(NULL);
}

static void model_done(tctx *t)
{
    scene *s = (scene *)t->p;
    if (s) {
        sc_free(s);
        free(s);
    }
    t->p = NULL;
}

#define MODEL(id, file, what) { id, "model", what, T_DATA, 0, file, model_setup, model_frame, model_done }
const test_def model_tests[] = {
    MODEL("M1TP4K", "TPOT4K.DBS", "Utah teapot, 4.5k triangles, Gouraud"),
    MODEL("M1TP16", "TPOT16K.DBS", "Utah teapot, 16k triangles, Gouraud"),
    MODEL("M1TP64", "TPOT64K.DBS", "Utah teapot, 65k triangles, Gouraud"),
    MODEL("M2BUNY", "BUNNY.DBS", "Stanford bunny, 69k triangles"),
    MODEL("M2DRGN", "DRAGON.DBS", "Stanford dragon, 48k triangles"),
    MODEL("M3LANT", "LANTERN.DBS", "Lantern, 5.4k triangles, textured"),
    MODEL("M3SUZ", "SUZANNE.DBS", "Suzanne, 3.9k triangles, textured"),
    MODEL("M3AVOC", "AVOCADO.DBS", "Avocado, 682 triangles, textured"),
    { NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL }
};
