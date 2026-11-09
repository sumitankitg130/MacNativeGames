/* Direct TotK 1.4.3 mouse camera, adapted from dxnside/MacNativeGames
   366aa07819fe595d5bd3d66fd17a29d4eca0fdb7 (MIT; see THIRD_PARTY_NOTICES.md).
   Included only by game_patches_v143.c, after its checked branch encoder.
   The camera layouts and four guarded sites come from that contribution.
   Mouse input is serialized separately from gamepadLock; the guest callback
   takes that lock only to consume input, then edits the active camera on its
   own thread. No camera pointer survives the callback. */
#pragma once
#include "runtime.h"
#include <TargetConditionals.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !TARGET_OS_IPHONE
typedef struct { uint64_t x[31], nzcv; } NativeMouseFrame;
_Static_assert(sizeof(NativeMouseFrame)==256,"native mouse saved register layout");
typedef struct {
    int on;
    double dx, dy, scale_yaw, scale_pitch;
    uint64_t last_move, hold_until;
} NativeMouseInput;
static NativeMouseInput nativeMouseInput;
static pthread_mutex_t nativeMouseLock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int nativeMouseReady;
static int nativeMouseLog;   /* TOTK_NATIVE_MOUSE_LOG=1, diagnosis only */

static void native_mouse_clear_locked(void) {
    nativeMouseInput.dx = nativeMouseInput.dy = 0;
    nativeMouseInput.last_move = nativeMouseInput.hold_until = 0;
}
static float native_mouse_float(uintptr_t address) {
    float v; memcpy(&v, (void *)address, sizeof v); return v;
}
static void native_mouse_store(uintptr_t address, float v) { memcpy((void *)address, &v, sizeof v); }

static void native_mouse_hook(NativeMouseFrame *f, uint64_t kind) {
    if (kind < 1 || kind > 4) return;
    uintptr_t r19=f->x[19], r20=f->x[20], r22=f->x[22], r23=f->x[23], r24=f->x[24], r26=f->x[26], r29=f->x[29];
    uintptr_t cam = kind==1 ? r26 : kind==2 ? r22 : kind==3 ? r24 : r23;
    if (!cam || !r19 || (kind==1 && !r20) || (kind==4 && r29<0x44)) return;
    pthread_mutex_lock(&nativeMouseLock);
    uint64_t now = runtime_now_ns();
    NativeMouseInput *input = &nativeMouseInput;
    if (!input->on) { pthread_mutex_unlock(&nativeMouseLock); return; }
    /* Drop input queued during a stalled/menu camera, not the first movement
       after the player rests the mouse. Keep fractional AppKit deltas. */
    if (now-input->last_move > 125000000ull) input->dx=input->dy=0;
    float yaw_delta=(float)(input->dx*input->scale_yaw), pitch_delta=(float)(input->dy*input->scale_pitch);
    input->dx=input->dy=0;
    if (yaw_delta || pitch_delta) input->hold_until=now+333000000ull;
    int hold = now<input->hold_until;
    pthread_mutex_unlock(&nativeMouseLock);

    if (yaw_delta || pitch_delta) {
        uintptr_t yaw = kind==1 ? r26+0x414 : kind==2 ? r22+0x414 : kind==3 ? r19+0xbc : r29-0x44;
        uintptr_t pitch = kind==1 ? r20+0x108 : kind==2 ? r19+0x15c : kind==3 ? r19+0xa4 : r19+0x50;
        uintptr_t pitch2 = kind==2 ? r19+0x154 : kind==4 ? r19+0x160 : 0;
        uintptr_t bounds = kind==1 ? r20 : kind==2 || kind==4 ? r19 : 0;
        if (kind==4) {
            uint32_t flags; memcpy(&flags,(void *)(cam+0x15c),sizeof flags);
            if (flags & (1u<<8)) yaw=r19+0x54;  /* Recall's base yaw */
        }
        float y=remainderf(native_mouse_float(yaw)-yaw_delta,360.0f);
        if (y>=180.0f) y-=360.0f;
        native_mouse_store(yaw,y);
        float p=native_mouse_float(pitch)+pitch_delta;
        if (bounds) {
            unsigned lo=kind==1 ? 0xf8 : kind==2 ? 0x1d4 : 0x1ac;
            unsigned hi=kind==1 ? 0xfc : kind==2 ? 0x1d8 : 0x1b0;
            p=fminf(fmaxf(p,native_mouse_float(bounds+lo)),native_mouse_float(bounds+hi));
        }
        native_mouse_store(pitch,p);
        if (pitch2) native_mouse_store(pitch2,p);
        if(nativeMouseLog) fprintf(stderr,"[totk-input] native camera %llu: delta %.3f %.3f degrees, yaw %.3f pitch %.3f\n",
                                   (unsigned long long)kind,yaw_delta,pitch_delta,y,p);
    }
    if (hold && fabsf(native_mouse_float(cam+0x420))+fabsf(native_mouse_float(cam+0x424))==0) {
        /* Prevent immediate auto-centering, preserving real controller input. */
        native_mouse_store(cam+0x420,0.001f); native_mouse_store(cam+0x424,0.001f);
    }
}

typedef struct { uint32_t w[112]; unsigned n; } NativeMouseCode;
static void native_mouse_emit(NativeMouseCode *c,uint32_t w) {
    if(c->n>=sizeof c->w/sizeof *c->w) runtime_fail("native mouse veneer too large");
    c->w[c->n++]=w;
}
static uint32_t native_mouse_pair(unsigned a,unsigned b,unsigned off,int vector,int load) {
    uint32_t op=vector ? (load ? 0xad400000u : 0xad000000u) : (load ? 0xa9400000u : 0xa9000000u);
    return op | (((off/(vector?16:8))&0x7fu)<<15) | (b<<10) | (31<<5) | a;
}
static uint32_t native_mouse_single(unsigned rt,unsigned off,int load) {
    return (load?0xf9400000u:0xf9000000u) | ((off/8)<<10) | (31<<5) | rt;
}
static NativeMouseCode native_mouse_code(uint64_t stub,uint64_t site,uint32_t original,unsigned kind,void *hook) {
    NativeMouseCode c={0};
    /* 800 bytes: x0..x30, NZCV, q0..q31, FPCR and FPSR. Host work runs on
       the runtime's host stack, not the game's potentially tiny stack. */
    native_mouse_emit(&c,0xd10c83ff);             /* sub sp,sp,#800 */
    for(unsigned r=0;r<30;r+=2) native_mouse_emit(&c,native_mouse_pair(r,r+1,r*8,0,0));
    native_mouse_emit(&c,native_mouse_single(30,240,0));
    native_mouse_emit(&c,0xd53b4210); native_mouse_emit(&c,native_mouse_single(16,248,0));
    for(unsigned r=0;r<32;r+=2) native_mouse_emit(&c,native_mouse_pair(r,r+1,256+r*16,1,0));
    native_mouse_emit(&c,0xd53b4410); native_mouse_emit(&c,native_mouse_single(16,768,0)); /* FPCR */
    native_mouse_emit(&c,0xd53b4430); native_mouse_emit(&c,native_mouse_single(16,776,0)); /* FPSR */
    native_mouse_emit(&c,0x910003e0);             /* mov x0,sp */
    native_mouse_emit(&c,0x52800001u|(kind<<5));
    unsigned load=c.n; native_mouse_emit(&c,0);
    native_mouse_emit(&c,0xd63f0200);             /* blr x16 (host-stack wrapper) */
    native_mouse_emit(&c,native_mouse_single(16,768,1)); native_mouse_emit(&c,0xd51b4410);
    native_mouse_emit(&c,native_mouse_single(16,776,1)); native_mouse_emit(&c,0xd51b4430);
    native_mouse_emit(&c,native_mouse_single(16,248,1)); native_mouse_emit(&c,0xd51b4210);
    for(unsigned r=0;r<32;r+=2) native_mouse_emit(&c,native_mouse_pair(r,r+1,256+r*16,1,1));
    for(unsigned r=0;r<30;r+=2) native_mouse_emit(&c,native_mouse_pair(r,r+1,r*8,0,1));
    native_mouse_emit(&c,native_mouse_single(30,240,1));
    native_mouse_emit(&c,0x910c83ff);             /* add sp,sp,#800 */
    native_mouse_emit(&c,original);
    unsigned ret=c.n; native_mouse_emit(&c,branch(stub+ret*4,site+4));
    if(c.n&1) native_mouse_emit(&c,0xd503201f);
    unsigned literal=c.n; native_mouse_emit(&c,0); native_mouse_emit(&c,0);
    c.w[load]=0x58000010u|((literal-load)<<5);
    _Static_assert(sizeof hook==8,"native mouse callback pointer size");
    memcpy(c.w+literal,&hook,sizeof hook);
    return c;
}
static void patch_native_mouse_camera(Module *m) {
    const char *enabled=getenv("TOTK_NATIVE_MOUSE_CAMERA");
    if ((enabled && !strcmp(enabled,"0")) || getenv("TOTK_EXPORT_IOS")) return;
    const struct { uint64_t off; uint32_t original; } sites[]={
        {0x18e468,0x91103348}, {0x23be9b4,0x911032c8},
        {0x23d756c,0xbd442300}, {0x23d2dd0,0xb85bc3a9}
    };
    /* Lower 640 bytes belong to survey/effects patches. Upper 128 bytes
       belong to VR. Preflight every instruction and the entire reservation
       before writing anything; no partly installed camera on failure. */
    uint64_t start=((m->file_size+15)&~15ULL)+640;
    uint64_t end=(m->file_size+0x3fff)&~0x3fffULL;
    enum { RESERVED=4*sizeof(((NativeMouseCode *)0)->w) };
    if(end>m->size || start<m->patch_off || end<128 || start>end-128 || RESERVED>end-128-start) {
        fprintf(stderr,"[totk] native mouse: no free patch tail; keeping stick/gyro panning\n"); return;
    }
    for(unsigned i=0;i<4;i++) {
        uint32_t w;
        if(sites[i].off+4>m->text_end) runtime_fail("native mouse camera site outside text");
        memcpy(&w,m->base+sites[i].off,4);
        if(w!=sites[i].original) runtime_fail("1.4.3 native mouse camera instruction mismatch");
    }
    for(uint64_t p=start;p<start+RESERVED;p++)
        if(m->base[p]) runtime_fail("native mouse camera patch space occupied");
    void *hook=runtime_hle_wrap_named(native_mouse_hook,"native mouse camera");
    NativeMouseCode code[4]; uint32_t jumps[4]; uint64_t cursor=start;
    for(unsigned i=0;i<4;i++) {
        code[i]=native_mouse_code(cursor,sites[i].off,sites[i].original,i+1,hook);
        jumps[i]=branch(sites[i].off,cursor);
        cursor+=code[i].n*4;
    }
    cursor=start;
    for(unsigned i=0;i<4;i++) {
        memcpy(m->base+cursor,code[i].w,code[i].n*4);
        memcpy(m->base+sites[i].off,jumps+i,4); cursor+=code[i].n*4;
    }
    atomic_store_explicit(&nativeMouseReady,1,memory_order_release);
    const char *log=getenv("TOTK_NATIVE_MOUSE_LOG"); nativeMouseLog=log && !strcmp(log,"1");
    fprintf(stderr,"[totk] 1.4.3 native mouse camera: four guarded hooks, veneers +%#llx..+%#llx\n",
            (unsigned long long)start,(unsigned long long)cursor);
}
#else
static void patch_native_mouse_camera(Module *m) { (void)m; }
#endif

int runtime_mouse_camera_available(void) {
#if TARGET_OS_IPHONE
    return 0;
#else
    return atomic_load_explicit(&nativeMouseReady,memory_order_acquire);
#endif
}
void runtime_mouse_camera_clear(void) {
#if !TARGET_OS_IPHONE
    pthread_mutex_lock(&nativeMouseLock); native_mouse_clear_locked(); pthread_mutex_unlock(&nativeMouseLock);
#endif
}
void runtime_mouse_camera_set(int on,int sensitivity,int invert_y) {
#if !TARGET_OS_IPHONE
    pthread_mutex_lock(&nativeMouseLock);
    native_mouse_clear_locked();
    nativeMouseInput.on=!!on && runtime_mouse_camera_available();
    nativeMouseInput.scale_yaw=(sensitivity>=1 && sensitivity<=100 ? sensitivity : 50)/500.0;
    nativeMouseInput.scale_pitch=nativeMouseInput.scale_yaw*(invert_y ? 1 : -1);
    pthread_mutex_unlock(&nativeMouseLock);
#else
    (void)on; (void)sensitivity; (void)invert_y;
#endif
}
void runtime_mouse_camera_move(double dx,double dy) {
#if !TARGET_OS_IPHONE
    if(!isfinite(dx) || !isfinite(dy)) return;
    pthread_mutex_lock(&nativeMouseLock);
    if(nativeMouseInput.on && (dx || dy)) {
        nativeMouseInput.dx=fmax(-8192,fmin(8192,nativeMouseInput.dx+dx));
        nativeMouseInput.dy=fmax(-8192,fmin(8192,nativeMouseInput.dy+dy));
        nativeMouseInput.last_move=runtime_now_ns();
    }
    pthread_mutex_unlock(&nativeMouseLock);
#else
    (void)dx; (void)dy;
#endif
}
