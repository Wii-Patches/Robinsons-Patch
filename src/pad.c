/* GameCube controller and Classic Controller support for Meet the Robinsons.
 *
 * The game only knows a Wii Remote with a Nunchuk.  Everything here makes
 * another controller look like one to the game's copy of the Wii SDK input
 * library (KPAD), so the game's own code is never touched:
 *
 *   poll    KPADiRead entry         turns the Serial Interface's auto-polling on
 *                                   for the GameCube ports (the game never links PAD)
 *   sample  KPADiRead, before the   writes a GameCube pad, or a real Classic
 *           queued-sample check     Controller, into KPAD's sample ring as a Nunchuk
 *   post    KPADiRead, per sample   steers the Wii Remote pointer with the C-stick
 *   probe   WPADProbe entry         reports a Nunchuk where there is none
 *
 * The approach follows Barrel Blast Patch (SI polling, hardware-tested) and the
 * City Folk pad (sample synthesis).  Addresses arrive as -D macros, resolved
 * per disc release by gen_pad.py.
 *
 * No floating point, no globals, no string literals: every hook is a
 * self-contained blob that works wherever a Gecko handler or the patcher puts it.
 */
typedef unsigned int u32;
typedef signed int s32;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned char u8;
typedef signed char s8;

#define R32(a) (*(volatile u32 *)(a))
#define SI_OUT(c)  (0xCD006400u + 12u * (c))
#define SI_INH(c)  (0xCD006404u + 12u * (c))
#define SI_INL(c)  (0xCD006408u + 12u * (c))
#define SI_POLL    (0xCD006430u)
#define SI_COMCSR  (0xCD006434u)
#define SI_SR      (0xCD006438u)

/* ---- controller layout: what the Wii Remote word looks like -------------- */
#define WM_LEFT  0x0001
#define WM_RIGHT 0x0002
#define WM_DOWN  0x0004
#define WM_UP    0x0008
#define WM_PLUS  0x0010
#define WM_TWO   0x0100
#define WM_ONE   0x0200
#define WM_B     0x0400
#define WM_A     0x0800
#define WM_MINUS 0x1000
#define WM_Z     0x2000        /* Nunchuk Z, merged into the same word */
#define WM_C     0x4000        /* Nunchuk C */
#define WM_HOME  0x8000

/* ---- one pad, whatever it is ---------------------------------------------- */
#define P_A      0x0001
#define P_B      0x0002
#define P_X      0x0004
#define P_Y      0x0008
#define P_Z      0x0010
#define P_L      0x0020
#define P_R      0x0040
#define P_START  0x0080
#define P_UP     0x0100
#define P_DOWN   0x0200
#define P_LEFT   0x0400
#define P_RIGHT  0x0800
#define P_ZL     0x1000
#define P_ZR     0x2000
#define P_MINUS  0x4000
#define P_HOME   0x8000

struct pin {
    u32 b;                  /* P_* */
    s32 lx, ly;             /* sticks, +-100 at full deflection like a Nunchuk's */
    s32 rx, ry;
};

struct st {
    u32 probe_tb[4];        /* last SIGetType per port */
    u32 busy_tb;            /* when si:: was first seen busy (0 = idle) */
    u8 norep[4];            /* consecutive frames with NOREP per port */
    u8 ours[4];             /* the last sample on this channel was synthesised */
    u8 src[4];              /* 0 = no pad, 1 = GameCube, 2 = Classic Controller */
    u8 pad_[4];
    u32 prev[4];            /* previous frame's Wii Remote word */
    s32 px[4], py[4];       /* pointer position, 1.0 = 0x10000 */
    u8 fl_n[4];             /* frames of Nunchuk flick left to report */
    s8 fl_dx[4], fl_dy[4];  /* its direction: +1 right / up */
    u8 fl_arm[4];           /* the stick has been back in the middle since the last flick */
};
#define ST ((volatile struct st *)STATE)

static inline u32 tb(void)
{
    u32 t;
    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
}

static inline s32 clampi(s32 v, s32 lo, s32 hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* a valid, error-free pad response on this port */
static inline int gc_in(u32 chan, u32 *h, u32 *l)
{
#ifdef DEBUG_FEED
    /* test builds: the response is written to STATE+0x80+8*chan by a debugger */
    u32 hh = R32(STATE + 0x80 + 8 * chan);
    if (!hh)
        return 0;
    *h = hh;
    *l = R32(STATE + 0x84 + 8 * chan);
    return 1;
#else
    u32 v = R32(SI_INH(chan));
    if ((v & 0x80000000u) || !(v & 0x00800000u))
        return 0;
    *h = v;
    *l = R32(SI_INL(chan));
    return 1;
#endif
}

#if defined(HOOK_POLL)
void pad_poll(u32 chan)
{
    volatile u32 *types = (volatile u32 *)SI_TYPES;
    u32 c, mask = 0, sisr, poll, now;
    s32 busy;

    if (chan)                       /* once per frame, from channel 0's call */
        return;

    sisr = R32(SI_SR);
    for (c = 0; c < 4; c++) {
        u32 type = types[c];
        int confirmed = !(type & 0x80) && (type & 0x18000000u) == 0x08000000u;

        /* probe a port until a standard pad answers, at most every 0.25 s:
         * probing every frame collided with the pad's own polling on hardware */
        if (!confirmed) {
            now = tb();
            if (now - ST->probe_tb[c] >= 15187500u) {
                ST->probe_tb[c] = now;
                ((u32 (*)(u32))FN_SIGETTYPE)(c);
            }
        }

        /* an unplugged pad latches NOREP; si:: never reads it (no PAD library),
         * so copy a persistent one into the type cache ourselves, which makes
         * SIGetType probe the port again once a pad is plugged back in */
        if (sisr & (0x08000000u >> (8 * c))) {
            if (ST->norep[c] < 40)
                ST->norep[c]++;
            else
                types[c] = 8;
        } else {
            ST->norep[c] = 0;
        }
    }

    for (c = 0; c < 4; c++)
        R32(SI_OUT(c)) = 0x00400300u;                 /* poll command */
    R32(SI_SR) = (sisr & 0x0F0F0F0Fu) | 0x80000000u;   /* ack errors, latch OUT */

    for (c = 0; c < 4; c++) {
        u32 type = types[c];
        if (!(type & 0x80) && (type & 0x18000000u) == 0x08000000u)
            mask |= 0x88u >> c;                        /* ENn + VBCPYn */
    }
    poll = R32(SI_POLL) & ~0xFFu;
    if (!(poll & 0xFF00u))
        poll |= 0x0100u;
    R32(SI_POLL) = poll | mask;
    /* si:: rewrites SIPOLL from its own shadow on every retrace */
    R32(SI_SHADOW) = (R32(SI_SHADOW) & ~0xFFu) | mask;

    /* a pad unplugged mid-transfer leaves si::'s global busy flag wedged
     * (nothing times it out); force it idle after a second */
    busy = (s32)R32(SI_BUSY);
    if (busy == -1) {
        ST->busy_tb = 0;
    } else {
        now = tb();
        if (ST->busy_tb == 0) {
            ST->busy_tb = now | 1;
        } else if (now - ST->busy_tb >= 60750000u) {
            u32 lvl = ((u32 (*)(void))FN_OSDISABLE)();
            R32(SI_BUSY) = (u32)-1;
            R32(SI_COMCSR) = 0x80000000u;
            ((void (*)(u32))FN_OSRESTORE)(lvl);
            ST->busy_tb = 0;
        }
    }
}
#endif

#if defined(HOOK_SAMPLE)
/* ---- the controls --------------------------------------------------------- */

/* Which Wii Remote + Nunchuk button each pad button presses. */
static __attribute__((noinline)) u32 to_wii(u32 p)
{
    u32 w = 0;

    if (p & P_A)     w |= WM_A;
    if (p & P_B)     w |= WM_B;
    if (p & P_X)     w |= WM_C;
    if (p & P_Y)     w |= WM_MINUS;
    if (p & (P_L | P_ZL))       w |= WM_Z;
    if (p & (P_R | P_ZR))       w |= WM_ONE;
    if (p & P_Z)     w |= WM_TWO;
    if (p & P_START) w |= WM_PLUS;
    if (p & P_MINUS) w |= WM_MINUS;
    if (p & P_HOME)  w |= WM_HOME;
    if (p & P_UP)    w |= WM_UP;
    if (p & P_DOWN)  w |= WM_DOWN;
    if (p & P_LEFT)  w |= WM_LEFT;
    if (p & P_RIGHT) w |= WM_RIGHT;
    return w;
}
#endif

#if defined(HOOK_SAMPLE)
static inline s32 gcs(u32 raw)       /* a pad stick byte -> +-100 */
{
    return clampi((s32)(raw & 0xFF) - 128, -127, 127);
}

static __attribute__((noinline)) void from_gc(struct pin *p, u32 h, u32 l)
{
    u32 b = 0;

    if (h & 0x01000000u) b |= P_A;
    if (h & 0x02000000u) b |= P_B;
    if (h & 0x04000000u) b |= P_X;
    if (h & 0x08000000u) b |= P_Y;
    if (h & 0x10000000u) b |= P_START;
    if (h & 0x00100000u) b |= P_Z;
    if (h & 0x00400000u) b |= P_L;
    if (h & 0x00200000u) b |= P_R;
    if (h & 0x00080000u) b |= P_UP;
    if (h & 0x00040000u) b |= P_DOWN;
    if (h & 0x00020000u) b |= P_RIGHT;
    if (h & 0x00010000u) b |= P_LEFT;
    p->b = b;
    p->lx = gcs(h >> 8);
    p->ly = gcs(h);
    p->rx = gcs(l >> 24);
    p->ry = gcs(l >> 16);
}

/* Classic Controller buttons as WPAD reports them */
#define CL_UP    0x0001
#define CL_LEFT  0x0002
#define CL_ZR    0x0004
#define CL_X     0x0008
#define CL_A     0x0010
#define CL_Y     0x0020
#define CL_B     0x0040
#define CL_ZL    0x0080
#define CL_R     0x0200
#define CL_PLUS  0x0400
#define CL_HOME  0x0800
#define CL_MINUS 0x1000
#define CL_L     0x2000
#define CL_DOWN  0x4000
#define CL_RIGHT 0x8000

static inline s32 ccs(s32 v)         /* a Classic Controller stick (+-308) -> +-100 */
{
    return clampi(v / 3, -127, 127);
}

static __attribute__((noinline)) void from_cc(struct pin *p, const u8 *s)
{
    u32 c = *(const u16 *)(s + 0x2A);
    u32 b = 0;

    if (c & CL_A)     b |= P_A;
    if (c & CL_B)     b |= P_B;
    if (c & CL_X)     b |= P_X;
    if (c & CL_Y)     b |= P_Y;
    if (c & CL_L)     b |= P_L;
    if (c & CL_R)     b |= P_R;
    if (c & CL_ZL)    b |= P_ZL;
    if (c & CL_ZR)    b |= P_ZR;
    if (c & CL_PLUS)  b |= P_START;
    if (c & CL_MINUS) b |= P_MINUS;
    if (c & CL_HOME)  b |= P_HOME;
    if (c & CL_UP)    b |= P_UP;
    if (c & CL_DOWN)  b |= P_DOWN;
    if (c & CL_LEFT)  b |= P_LEFT;
    if (c & CL_RIGHT) b |= P_RIGHT;
    p->b = b;
    p->lx = ccs(*(const s16 *)(s + 0x2C));
    p->ly = ccs(*(const s16 *)(s + 0x2E));
    p->rx = ccs(*(const s16 *)(s + 0x30));
    p->ry = ccs(*(const s16 *)(s + 0x32));
}

/* make `s` a Nunchuk sample carrying `p`.  keep: 0 = a fresh sample;
 * 1 = keep the Wii Remote's buttons and pointer; 2 = keep its buttons only */
static __attribute__((noinline)) void put_nunchuk(u8 *s, const struct pin *p, u32 wbtn, int keep)
{
    u32 i;

    if (!keep)
        for (i = 0; i < 0x38 / 4; i++)
            ((u32 *)s)[i] = 0;
    else
        for (i = (keep == 2 ? 8 : 0x28) / 4; i < 0x38 / 4; i++)   /* extension (and pointer) part */
            ((u32 *)s)[i] = 0;
    *(u16 *)s = (u16)((keep ? *(u16 *)s : 0) | wbtn);
    s[0x28] = 1;                                   /* extension: Nunchuk */
    s[0x29] = 0;                                   /* no extension error */
    s[0x30] = (u8)(s8)p->lx;                       /* stick, signed around the centre */
    s[0x31] = (u8)(s8)p->ly;
    s[0x36] = keep == 2 ? 4 : 5;                   /* nunchuk + accelerometer (+ pointer data) */
}

/* The game turns the camera by how far the pointer is from the middle of the
 * screen, so the C-stick works like a joystick: deflection = pointer offset,
 * and letting go brings the pointer (and the camera) back to rest. */
static __attribute__((noinline)) void point(u32 chan, const struct pin *p)
{
    s32 dx = p->rx, dy = p->ry;
    s32 tx, ty;
    s32 ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;

    /* Targeting Mode switches targets by flicking the Nunchuk towards the new
     * one.  With the lock (L / ZL) held, a hard push of the C-stick is that
     * flick: one short pulse per push, the stick has to come back first.
     * The pointer stays where it was meanwhile, so the camera does not swing
     * and the lock keeps what the pointer was on. */
    if (ax < 30 && ay < 30)
        ST->fl_arm[chan] = 1;
    if (ST->fl_n[chan])
        ST->fl_n[chan]--;
    if (p->b & (P_L | P_ZL)) {
        if (ST->fl_arm[chan] && (ax >= 80 || ay >= 80)) {
            ST->fl_arm[chan] = 0;
            ST->fl_n[chan] = 3;
            ST->fl_dx[chan] = ax >= ay ? (dx > 0 ? 1 : -1) : 0;
            ST->fl_dy[chan] = ax >= ay ? 0 : (dy > 0 ? 1 : -1);
        }
        return;
    }

    if (dx > -10 && dx < 10) dx = 0;               /* dead zone */
    if (dy > -10 && dy < 10) dy = 0;
    tx = clampi(dx * 700, -0x10000, 0x10000);
    ty = clampi(-dy * 700, -0x10000, 0x10000);
    ST->px[chan] += (tx - ST->px[chan]) / 2;       /* a little smoothing */
    ST->py[chan] += (ty - ST->py[chan]) / 2;
}

/* One frame of KPAD's sample ring for `chan` (k = its KPAD struct). */
void pad_sample(u8 *k, u32 chan)
{
    struct pin p;
    u32 h, l, i, idx, cnt, wb, src = 0;

    if (chan > 3)
        return;

    idx = k[0x10E] & 0xF;       /* the ring wraps at 16: 0x10 and 0 are the same slot */
    cnt = k[0x10F];
    if (cnt > 0x10)
        cnt = 0x10;

    /* a real Classic Controller on this channel takes precedence over a pad */
    for (i = 0; i < cnt; i++) {
        u8 *s = k + 0x110 + ((idx - cnt + i) & 0xF) * 0x38;
        if (s[0x28] == 2) {
            from_cc(&p, s);
            wb = to_wii(p.b);
            if (i == 0)
                point(chan, &p);
            /* the remote lies somewhere, perhaps facing the sensor bar: its
             * pointer is dropped so the right stick steers the camera */
            put_nunchuk(s, &p, wb, 2);
            src = 2;
        }
    }
    if (src) {
        ST->src[chan] = 2;
        ST->ours[chan] = 0;
        return;
    }

    if (!gc_in(chan, &h, &l)) {
        ST->src[chan] = 0;
        ST->ours[chan] = 0;
        return;
    }
    from_gc(&p, h, l);
    wb = to_wii(p.b);

    if (cnt == 0) {
        /* no sample queued: no Wii Remote (device type 0xFD), a bare one that has
         * not delivered one yet, or our own sample showing through */
        u8 dev = k[0x5C];
        if (!(dev == 0 || dev == 0xFD || ST->ours[chan]))
            return;
        point(chan, &p);
        for (i = 0; i < 3; i++)
            put_nunchuk(k + 0x110 + ((idx + i) & 0xF) * 0x38, &p, i < 2 ? ST->prev[chan] : wb, 0);
        k[0x10E] = (idx + 3) & 0xF;
        k[0x10F] = 3;
        ST->prev[chan] = wb;
        ST->ours[chan] = 1;
        ST->src[chan] = 1;
        return;
    }

    /* real samples queued: a bare Wii Remote gets the pad as its extension;
     * a real Nunchuk is never touched (and then the pad is ignored) */
    ST->ours[chan] = 0;
    ST->prev[chan] = wb;
    for (i = 0; i < cnt; i++) {
        u8 *s = k + 0x110 + ((idx - cnt + i) & 0xF) * 0x38;
        if (s[0x28] == 0 || s[0x28] == 0xFD) {
            put_nunchuk(s, &p, wb, 1);
            src = 1;
        }
    }
    if (src) {
        point(chan, &p);
        ST->src[chan] = 1;
    } else {
        ST->src[chan] = 0;
    }
}
#endif

#if defined(HOOK_POST)
/* float 1.0 = 0x10000 in fixed point -> IEEE-754 bits, no FPU needed */
static __attribute__((noinline)) u32 fx2f(s32 v)
{
    u32 sign = 0, m;
    int e;

    if (!v)
        return 0;
    if (v < 0) {
        sign = 0x80000000u;
        v = -v;
    }
    e = 31 - __builtin_clz((u32)v);
    m = e >= 23 ? ((u32)v >> (e - 23)) : ((u32)v << (23 - e));
    return sign | ((u32)(127 + e - 16) << 23) | (m & 0x7FFFFFu);
}

#define F_TWO 0x40000000u                           /* 2.0f: past the game's 1.25 g flick threshold */

/* KPADiRead, once per sample: the Nunchuk's motion, and the Wii Remote pointer */
void pad_post(u8 *k, u32 chan)
{
    u32 *acc = (u32 *)(k + 0x68);                   /* Nunchuk acc x, y, z, then |acc| and its speed */
    u32 i, n;

    if (chan > 3 || !ST->src[chan])
        return;

    /* The Nunchuk here is ours, so is its accelerometer: still, unless a flick
     * is being reported.  In game, +x switches to the target on the right
     * (checked on the frog stage); -y is taken as up (not observed). */
    for (i = 0; i < 5; i++)
        acc[i] = 0;
    n = ST->fl_n[chan];
    if (n) {
        if (ST->fl_dx[chan])
            acc[0] = ST->fl_dx[chan] > 0 ? F_TWO : (F_TWO | 0x80000000u);
        if (ST->fl_dy[chan])
            acc[1] = ST->fl_dy[chan] > 0 ? (F_TWO | 0x80000000u) : F_TWO;
        acc[3] = F_TWO;
        acc[4] = F_TWO;
    }

    /* A real pointer wins next to a GameCube pad (the remote may be in hand).
     * With a Classic Controller the remote is put down, often still facing
     * the sensor bar, so the right stick always drives the pointer. */
    if (k[0x5E] && ST->src[chan] != 2)
        return;
    *(u32 *)(k + 0x20) = fx2f(ST->px[chan]);
    *(u32 *)(k + 0x24) = fx2f(ST->py[chan]);
    k[0x5E] = 2;                                    /* pointer valid */
}
#endif

#if defined(HOOK_PROBE)
/* WPADProbe(chan, &type): report a Nunchuk while a pad is plugged in, or a
 * Classic Controller is attached -- unless the channel has a real Nunchuk */
u32 pad_probe(u32 chan, u32 *type)
{
    u32 h, l;
    u8 *blk;
    s32 status;
    u32 t;

    if (chan > 3)
        return 0;
    blk = *(u8 **)(WPAD_TBL + chan * 4);
    status = *(s32 *)(blk + WPAD_STATUS);
    t = blk[WPAD_EXT];
    if (status == 0 && t == 1)
        return 0;
    if (status == 0 && t == 2) {
        if (type)
            *type = 1;
        return 1;
    }
    if (!gc_in(chan, &h, &l))
        return 0;
    if (type)
        *type = 1;
    return 1;
}
#endif
