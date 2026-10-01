/* scene.c - .DBS loading. The whole file is read into one buffer and the
 * sections are used in place (every section is 4-byte aligned; x86 is
 * little-endian like the format). */
#include "scene.h"
#include "vmath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#define FAIL(...) do { snprintf(err, (size_t)errlen, __VA_ARGS__); return -1; } while (0)

/* The version 2 records are used in place: their sizes are the format's. */
typedef char sc_size_check[(sizeof(sc_ghdr) == 88 && sizeof(sc_model) == 32 && sizeof(sc_inst) == 80 &&
                            sizeof(sc_part) == 64 && sizeof(sc_emit) == 48 && sizeof(sc_fxev) == 32 &&
                            sizeof(sc_cam) == 64 && sizeof(sc_surf) == 32 && sizeof(sc_batch) == 48) ? 1 : -1];

/* A section of n fixed-size records after a u32 count. */
static const void *records(const uint8_t *p, uint32_t len, uint32_t size, int *n)
{
    if (len < 4 || 4 + (uint64_t)u32(p) * size > len)
        return NULL;
    *n = (int)u32(p);
    return p + 4;
}

static int parse_tracks(scene *s, const uint8_t *p, uint32_t len, char *err, int errlen)
{
    uint32_t n, k, o = 4;
    if (len < 4)
        FAIL("TRAK too short");
    n = u32(p);
    s->track = (sc_track *)calloc(n ? n : 1, sizeof *s->track);
    if (!s->track)
        FAIL("out of memory");
    for (k = 0; k < n; k++) {
        sc_track *t = &s->track[k];
        if (o + 12 > len)
            FAIL("TRAK truncated");
        t->nkeys = u32(p + o);
        t->flags = u32(p + o + 4);
        memcpy(&t->length, p + o + 8, 4);
        t->keys = (const float *)(p + o + 12);
        o += 12 + t->nkeys * 16;
        if (t->nkeys < 2 || o > len)
            FAIL("TRAK track %u bad", (unsigned)k);
    }
    s->ntrack = (int)n;
    return 0;
}

static int parse_anims(scene *s, const uint8_t *p, uint32_t len, char *err, int errlen)
{
    uint32_t n, k, o = 4;
    if (len < 4)
        FAIL("VANM too short");
    n = u32(p);
    s->anim = (sc_anim *)calloc(n ? n : 1, sizeof *s->anim);
    if (!s->anim)
        FAIL("out of memory");
    for (k = 0; k < n; k++) {
        sc_anim *a = &s->anim[k];
        if (o + 36 > len)
            FAIL("VANM truncated");
        a->nverts = u32(p + o);
        a->nframes = u32(p + o + 4);
        a->vfirst = u32(p + o + 8);
        memcpy(a->scale, p + o + 12, 12);
        memcpy(a->origin, p + o + 24, 12);
        a->data = p + o + 36;
        o += 36 + a->nframes * a->nverts * 4;
        if (!a->nframes || o > len)
            FAIL("VANM animation %u bad", (unsigned)k);
    }
    s->nanim = (int)n;
    return 0;
}

static int parse_normals(scene *s, const uint8_t *p, uint32_t len, char *err, int errlen)
{
    uint32_t n, nv;
    if (len < 4)
        FAIL("NRML too short");
    n = u32(p);
    if (n > 256 || 8 + n * 12 > len)
        FAIL("NRML table bad");
    s->nnormal = (int)n;
    s->normal = (const float *)(p + 4);
    nv = u32(p + 4 + n * 12);
    if (8 + n * 12 + nv > len)
        FAIL("NRML truncated");
    s->vnormal = p + 8 + n * 12;
    if ((long)nv != s->nvert)
        FAIL("NRML has %u vertices, VERT %ld", (unsigned)nv, s->nvert);
    return 0;
}

#define NONE_OR(v, n) ((v) == SC_NONE || (int)(v) < (n))

/* Every index a version 2 file holds points at something. */
static int check_v2(scene *s, char *err, int errlen)
{
    int i;
    const sc_ghdr *g = s->ghdr;
    if (!g->frames || g->rate <= 0)
        FAIL("GHDR frames %u rate %g", (unsigned)g->frames, (double)g->rate);
    if (!NONE_OR(g->sky, s->nmodel))
        FAIL("GHDR sky model %u", (unsigned)g->sky);
    for (i = 0; i < s->nmodel; i++) {
        const sc_model *m = &s->model[i];
        if (m->batch0 + m->nbatch > s->nbatch || !NONE_OR(m->lod_next, s->nmodel) || m->lod_next == (uint16_t)i)
            FAIL("model %d out of range", i);
        if ((m->flags & SC_MF_LIT) && !s->vnormal)
            FAIL("model %d is lit but there is no NRML", i);
        if ((m->flags & SC_MF_ANIM) && m->anim >= s->nanim)
            FAIL("model %d animation %u", i, (unsigned)m->anim);
    }
    for (i = 0; i < s->nanim; i++)
        if ((long)(s->anim[i].vfirst + s->anim[i].nverts) > s->nvert)
            FAIL("animation %d vertices out of range", i);
    for (i = 0; i < s->ninst; i++) {
        const sc_inst *n = &s->inst[i];
        if (n->model >= s->nmodel || !(n->parent == SC_NONE || n->parent < i) || !NONE_OR(n->track, s->ntrack) ||
            (n->kind == SC_IK_TRACK && n->track == SC_NONE) || n->kind > SC_IK_ORBIT)
            FAIL("instance %d out of range", i);
    }
    for (i = 0; i < s->npart; i++)
        if (!NONE_OR(s->part[i].tex, s->ntex) || s->part[i].life <= 0)
            FAIL("particle kind %d bad", i);
    for (i = 0; i < s->nemit; i++)
        if (s->emit[i].part >= s->npart || !NONE_OR(s->emit[i].inst, s->ninst) || s->emit[i].rate <= 0)
            FAIL("emitter %d bad", i);
    for (i = 0; i < s->nfxev; i++)
        if (s->fxev[i].part >= s->npart || !NONE_OR(s->fxev[i].inst, s->ninst))
            FAIL("burst %d bad", i);
    for (i = 0; i < s->ncam; i++) {
        const sc_cam *c = &s->cam[i];
        if (!NONE_OR(c->target, s->ninst) || !NONE_OR(c->track, s->ntrack) || !NONE_OR(c->look_track, s->ntrack) ||
            c->kind > SC_CK_ORBIT || (c->kind == SC_CK_PATH && c->track == SC_NONE) ||
            ((c->kind == SC_CK_CHASE || c->kind == SC_CK_MOUNT) && c->target == SC_NONE))
            FAIL("shot %d bad", i);
    }
    if (!s->ncam)
        FAIL("no CAMS shots");
    for (i = 0; i < s->nsurf; i++)
        if (s->surf[i].batch >= s->nbatch || s->surf[i].kind > SC_SK_PULSE)
            FAIL("surface %d bad", i);
    return 0;
}

static int parse_view(scene *s, const uint8_t *p, uint32_t len, char *err, int errlen)
{
    sc_view *v = &s->view;
    if (len < 32)
        FAIL("VIEW too short");
    v->kind = u32(p);
    v->frames = u32(p + 4);
    memcpy(&v->znear, p + 8, 4);
    memcpy(&v->zfar, p + 12, 4);
    v->fog_rgb = u32(p + 16);
    memcpy(&v->fog_start, p + 20, 4);
    memcpy(&v->fog_end, p + 24, 4);
    v->nkeys = u32(p + 28);
    if (v->kind == 0) {
        if (len < 32 + 20)
            FAIL("VIEW orbit too short");
        v->orbit = (const float *)(p + 32);
    } else {
        if (v->nkeys < 2 || len < 32 + v->nkeys * 24)
            FAIL("VIEW path too short");
        v->keys = (const float *)(p + 32);
    }
    return 0;
}

int sc_parse(scene *s, uint8_t *buf, long size, char *err, int errlen)
{
    uint32_t nsec, i;
    long off = 16;
    memset(s, 0, sizeof *s);
    s->file = buf;
    s->size = size;
    if (size < 16 || memcmp(buf, "DBS1", 4) || u32(buf + 4) < 1 || u32(buf + 4) > 2)
        FAIL("not a version 1 or 2 DBS file");
    s->version = (int)u32(buf + 4);
    nsec = u32(buf + 8);
    for (i = 0; i < nsec; i++) {
        const uint8_t *p;
        uint32_t len;
        if (off + 8 > size)
            FAIL("truncated section table");
        len = u32(buf + off + 4);
        p = buf + off + 8;
        if ((long)len > size - off - 8)
            FAIL("section %.4s overruns the file", (const char *)(buf + off));
        if (!memcmp(buf + off, "TEXS", 4)) {
            uint32_t n = u32(p), k, o = 4;
            s->tex = (sc_texture *)calloc(n ? n : 1, sizeof *s->tex);
            if (!s->tex)
                FAIL("out of memory");
            for (k = 0; k < n; k++) {
                if (o + 8 > len)
                    FAIL("TEXS truncated");
                s->tex[k].w = p[o] | (p[o + 1] << 8);
                s->tex[k].h = p[o + 2] | (p[o + 3] << 8);
                s->tex[k].flags = p[o + 4] | (p[o + 5] << 8);
                s->tex[k].rgba = p + o + 8;
                o += 8 + (uint32_t)s->tex[k].w * s->tex[k].h * 4;
                if (o > len)
                    FAIL("TEXS texture %u truncated", (unsigned)k);
            }
            s->ntex = (int)n;
        } else if (!memcmp(buf + off, "VERT", 4)) {
            s->nvert = (long)u32(p);
            if (4 + s->nvert * 32 > (long)len)
                FAIL("VERT truncated");
            s->vert = (const rb_vertex *)(p + 4);
        } else if (!memcmp(buf + off, "INDX", 4)) {
            s->nidx = (long)u32(p);
            if (4 + s->nidx * 2 > (long)len)
                FAIL("INDX truncated");
            s->idx = (const uint16_t *)(p + 4);
        } else if (!memcmp(buf + off, "BTCH", 4)) {
            s->nbatch = (int)u32(p);
            if (4 + (long)s->nbatch * 48 > (long)len)
                FAIL("BTCH truncated");
            s->batch = (const sc_batch *)(p + 4);
        } else if (!memcmp(buf + off, "VIEW", 4)) {
            if (parse_view(s, p, len, err, errlen) < 0)
                return -1;
        } else if (!memcmp(buf + off, "VISL", 4)) {
            s->vis = p;
            s->vis_size = (long)len;
        } else if (!memcmp(buf + off, "INFO", 4)) {
            s->info = (const char *)p;
            s->info_size = (long)len;
        } else if (s->version >= 2) {
            const char *tag = (const char *)(buf + off);
            int bad = 0;
            if (!memcmp(tag, "GHDR", 4)) {
                if (len < sizeof(sc_ghdr))
                    FAIL("GHDR too short");
                s->ghdr = (const sc_ghdr *)p;
            } else if (!memcmp(tag, "MODL", 4))
                bad = !(s->model = (const sc_model *)records(p, len, sizeof(sc_model), &s->nmodel));
            else if (!memcmp(tag, "INST", 4))
                bad = !(s->inst = (const sc_inst *)records(p, len, sizeof(sc_inst), &s->ninst));
            else if (!memcmp(tag, "PART", 4))
                bad = !(s->part = (const sc_part *)records(p, len, sizeof(sc_part), &s->npart));
            else if (!memcmp(tag, "EMIT", 4))
                bad = !(s->emit = (const sc_emit *)records(p, len, sizeof(sc_emit), &s->nemit));
            else if (!memcmp(tag, "FXEV", 4))
                bad = !(s->fxev = (const sc_fxev *)records(p, len, sizeof(sc_fxev), &s->nfxev));
            else if (!memcmp(tag, "CAMS", 4))
                bad = !(s->cam = (const sc_cam *)records(p, len, sizeof(sc_cam), &s->ncam));
            else if (!memcmp(tag, "SURF", 4))
                bad = !(s->surf = (const sc_surf *)records(p, len, sizeof(sc_surf), &s->nsurf));
            else if (!memcmp(tag, "TRAK", 4)) {
                if (parse_tracks(s, p, len, err, errlen) < 0)
                    return -1;
            } else if (!memcmp(tag, "VANM", 4)) {
                if (parse_anims(s, p, len, err, errlen) < 0)
                    return -1;
            } else if (!memcmp(tag, "NRML", 4)) {
                if (!s->vert)
                    FAIL("NRML before VERT");
                if (parse_normals(s, p, len, err, errlen) < 0)
                    return -1;
            }
            if (bad)
                FAIL("%.4s truncated", tag);
        }
        off += 8 + len + ((4 - (len & 3)) & 3);
    }
    for (i = 0; i < (uint32_t)s->nbatch; i++) {
        const sc_batch *b = &s->batch[i];
        if ((long)(b->vfirst + b->vcount) > s->nvert || (long)(b->ifirst + b->icount) > s->nidx ||
            b->vcount > 65536u || (b->tex != SC_NONE && b->tex >= s->ntex) || (b->lm != SC_NONE && b->lm >= s->ntex))
            FAIL("batch %u out of range", (unsigned)i);
        s->tris += b->icount / 3;
    }
    if (s->ghdr)
        return check_v2(s, err, errlen);
    if (!s->view.frames)
        FAIL("no VIEW section");
    return 0;
}

int sc_load(scene *s, const char *path, char *err, int errlen)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long size;
    memset(s, 0, sizeof *s);
    if (!f)
        FAIL("cannot open %s", path);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (uint8_t *)malloc((size_t)size + 4);
    if (!buf) {
        fclose(f);
        FAIL("%s: out of memory (%ld bytes)", path, size);
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(buf);
        FAIL("%s: read error", path);
    }
    fclose(f);
    if (sc_parse(s, buf, size, err, errlen) < 0) {
        sc_free(s);
        return -1;
    }
    return 0;
}

int sc_upload(scene *s, int meshes)
{
    int i;
    s->rtex = (rb_tex **)calloc((size_t)(s->ntex ? s->ntex : 1), sizeof *s->rtex);
    s->mesh = (rb_mesh **)calloc((size_t)(s->nbatch ? s->nbatch : 1), sizeof *s->mesh);
    if (!s->rtex || !s->mesh)
        return -1;
    for (i = 0; i < s->ntex; i++) {
        int fl = (s->tex[i].flags & SC_TF_MIPMAP ? RB_TF_MIPMAP : 0) | (s->tex[i].flags & SC_TF_CLAMP ? RB_TF_CLAMP : 0);
        s->rtex[i] = rb_tex_create(s->tex[i].w, s->tex[i].h, s->tex[i].rgba, fl);
        if (!s->rtex[i])
            return -1;
    }
    if (meshes)
        for (i = 0; i < s->nbatch; i++) {
            const sc_batch *b = &s->batch[i];
            s->mesh[i] = rb_mesh_create(s->vert + b->vfirst, (int)b->vcount, s->idx + b->ifirst, (int)b->icount);
            if (!s->mesh[i])
                return -1;
        }
    return 0;
}

void sc_free(scene *s)
{
    int i;
    if (s->mesh)
        for (i = 0; i < s->nbatch; i++)
            rb_mesh_free(s->mesh[i]);
    if (s->rtex)
        for (i = 0; i < s->ntex; i++)
            rb_tex_free(s->rtex[i]);
    free(s->mesh);
    free(s->rtex);
    free(s->tex);
    free(s->track);
    free(s->anim);
    free(s->file);
    memset(s, 0, sizeof *s);
}

void sc_camera(const scene *s, int f, float eye[3], float at[3])
{
    const sc_view *v = &s->view;
    if (v->kind == 0) {
        const float *o = v->orbit;
        double a = 2.0 * VM_PI * (f % (int)v->frames) / v->frames;
        eye[0] = o[0] + o[3] * (float)sin(a);
        eye[1] = o[1] + o[4];
        eye[2] = o[2] + o[3] * (float)cos(a);
        at[0] = o[0]; at[1] = o[1]; at[2] = o[2];
    } else {
        /* Catmull-Rom through the keys (equally spaced along the path by
         * tools/bsp.py), end to end, then from the start again. */
        uint32_t n = v->nkeys;
        double t = (double)(f % (int)v->frames) * (n - 1) / (v->frames > 1 ? v->frames - 1 : 1);
        uint32_t k = (uint32_t)t;
        uint32_t k0, k1, k2, k3;
        float u;
        const float *K = v->keys;
        if (k >= n - 1)
            k = n - 2;
        u = (float)(t - k);
        k0 = k ? k - 1 : 0;
        k1 = k;
        k2 = k + 1;
        k3 = k + 2 < n ? k + 2 : n - 1;
        v3_catmull(eye, K + k0 * 6, K + k1 * 6, K + k2 * 6, K + k3 * 6, u);
        v3_catmull(at, K + k0 * 6 + 3, K + k1 * 6 + 3, K + k2 * 6 + 3, K + k3 * 6 + 3, u);
    }
}

const char *sc_info(const scene *s, const char *key, char *buf, int buflen)
{
    long i = 0, kl = (long)strlen(key);
    buf[0] = 0;
    while (s->info && i < s->info_size) {
        long e = i;
        while (e < s->info_size && s->info[e] != '\n')
            e++;
        if (e - i > kl && !strncmp(s->info + i, key, (size_t)kl) && s->info[i + kl] == '=') {
            long n = e - i - kl - 1;
            if (n >= buflen)
                n = buflen - 1;
            memcpy(buf, s->info + i + kl + 1, (size_t)n);
            buf[n] = 0;
            break;
        }
        i = e + 1;
    }
    return buf;
}
