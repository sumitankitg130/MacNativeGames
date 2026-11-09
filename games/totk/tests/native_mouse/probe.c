/* Synthetic camera objects and real ARM64 veneer execution. No game files. */
#include "../../runtime/runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <libkern/OSCacheControl.h>
static uint64_t now=1000000000;
uint64_t runtime_now_ns(void) { return now; }
_Noreturn void runtime_fail(const char *s) { fprintf(stderr,"%s\n",s); exit(1); }
static uint32_t branch(uint64_t pc,uint64_t target) {
    int64_t d=(int64_t)target-(int64_t)pc;
    if((d&3) || d<-(1<<27) || d>=(1<<27)) runtime_fail("branch out of range");
    return 0x14000000u|((uint32_t)(d>>2)&0x3ffffff);
}
static unsigned wrappers;
void *runtime_hle_wrap_named(void *fn,const char *name) {
    if(strcmp(name,"native mouse camera")) runtime_fail("missing wrapper name");
    wrappers++; return fn;
}
#include "../../runtime/mouse_camera_v143.h"
static unsigned checked;
static void check(int ok,const char *s) { checked++; if(!ok) runtime_fail(s); }
static void near(float got,float want) { check(fabsf(got-want)<0.0001f,"camera angle differs"); }
static float objects[7][512];
static NativeMouseFrame frame;
static uintptr_t object(unsigned n) { return (uintptr_t)objects[n]; }
static void reset(void) {
    memset(objects,0,sizeof objects); memset(&frame,0,sizeof frame);
    frame.x[19]=object(0); frame.x[20]=object(1); frame.x[22]=object(2);
    frame.x[23]=object(3); frame.x[24]=object(4); frame.x[26]=object(5); frame.x[29]=object(6)+0x100;
    for(unsigned i=0;i<7;i++) {
        native_mouse_store(object(i)+0xf8,-80); native_mouse_store(object(i)+0xfc,80);
        native_mouse_store(object(i)+0x1d4,-70); native_mouse_store(object(i)+0x1d8,70);
        native_mouse_store(object(i)+0x1ac,-60); native_mouse_store(object(i)+0x1b0,60);
    }
    atomic_store(&nativeMouseReady,1); runtime_mouse_camera_set(1,50,0);
}
static unsigned called;
static void *queue_movement(void *unused) {
    (void)unused;
    for(unsigned i=0;i<500;i++) runtime_mouse_camera_move(.1,.2);
    return NULL;
}
static void clobber(NativeMouseFrame *f,uint64_t kind) {
    check(f->x[9]==0x1234 && kind==3,"veneer callback arguments"); called++;
    __asm__ volatile("mov x9,#1\n movi v31.16b,#0\n msr nzcv,xzr\n msr fpsr,xzr\n msr fpcr,xzr" ::: "x9","v31","cc");
}
static void execute_veneer(void) {
    unsigned char *page=mmap(NULL,0x4000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    check(page!=MAP_FAILED,"map synthetic veneer");
    NativeMouseCode c=native_mouse_code(0x400,0x100,0xd28acf08,3,clobber); /* displaced mov x8,#0x5678 */
    memcpy(page+0x400,c.w,c.n*4);
    uint32_t ret=0xd65f03c0; memcpy(page+0x104,&ret,4);
    sys_icache_invalidate(page,0x4000);
    check(!mprotect(page,0x4000,PROT_READ|PROT_EXEC),"protect synthetic veneer");
    _Alignas(16) uint64_t result[8]={0};
    register void *entry __asm__("x19")=page+0x400;
    register void *out __asm__("x20")=result;
    __asm__ volatile(
        "mrs x10,fpcr\n str x10,[%[out],#56]\n"
        "mov x10,#0x1000000\n msr fpcr,x10\n"
        "mov x9,#0x1234\n movi v31.16b,#0x5a\n"
        "mov x10,#0xa0000000\n msr nzcv,x10\n"
        "mov x10,#0x10\n msr fpsr,x10\n"
        "blr %[entry]\n str x9,[%[out]]\n str x8,[%[out],#8]\n str q31,[%[out],#16]\n"
        "mrs x10,nzcv\n str x10,[%[out],#32]\n"
        "mrs x10,fpsr\n str x10,[%[out],#40]\n msr fpsr,xzr\n"
        "mrs x10,fpcr\n str x10,[%[out],#48]\n ldr x10,[%[out],#56]\n msr fpcr,x10\n"
        : [entry] "+r"(entry), [out] "+r"(out)
        : : "x0","x1","x2","x3","x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","x30",
            "v0","v1","v2","v3","v4","v5","v6","v7","v8","v9","v10","v11","v12","v13","v14","v15",
            "v16","v17","v18","v19","v20","v21","v22","v23","v24","v25","v26","v27","v28","v29","v30","v31","cc","memory");
    check(called==1 && result[0]==0x1234,"veneer restores integer registers");
    check(result[1]==0x5678,"displaced instruction executes before returning");
    check(result[2]==0x5a5a5a5a5a5a5a5aull && result[3]==result[2],"veneer restores all 128 SIMD bits");
    check(result[4]==0xa0000000 && result[5]==0x10,"veneer restores NZCV and FPSR");
    check(result[6]==0x1000000,"veneer restores FPCR");
    munmap(page,0x4000);
}
static void patch_checks(const char *mode) {
    size_t file=0x3000100, end=(file+0x3fff)&~0x3fffULL;
    Module m={.name="main",.file_size=file,.size=end,.text_end=0x2800000,.patch_off=0x2800000};
    m.base=calloc(1,end); check(m.base!=NULL,"allocate synthetic module");
    const unsigned sites[]={0x18e468,0x23be9b4,0x23d756c,0x23d2dd0};
    const uint32_t words[]={0x91103348,0x911032c8,0xbd442300,0xb85bc3a9};
    for(unsigned i=0;i<4;i++) memcpy(m.base+sites[i],words+i,4);
    memset(m.base+file,0xa5,640); memset(m.base+end-128,0x5a,128);
    atomic_store(&nativeMouseReady,0);
    if(!strcmp(mode,"bad-site")) m.base[sites[3]]^=1;
    if(!strcmp(mode,"occupied")) m.base[file+640+100]=1;
    if(!strcmp(mode,"short-tail")) m.size=end-1;
    if(!strcmp(mode,"legacy")) setenv("TOTK_NATIVE_MOUSE_CAMERA","0",1);
    if(!strcmp(mode,"ios-export")) setenv("TOTK_EXPORT_IOS","1",1);
    patch_native_mouse_camera(&m);
    int expect=!strcmp(mode,"normal");
    check(runtime_mouse_camera_available()==expect,"availability follows install");
    check(wrappers==(unsigned)expect,"hook uses runtime host-stack wrapper");
    for(unsigned i=0;i<4;i++) {
        uint32_t w; memcpy(&w,m.base+sites[i],4);
        check(expect ? (w&0xfc000000)==0x14000000 : w==words[i],"guarded camera branch");
    }
    for(unsigned i=0;i<640;i++) check(m.base[file+i]==0xa5,"effects reservation preserved");
    for(unsigned i=0;i<128;i++) check(m.base[end-128+i]==0x5a,"VR reservation preserved");
    free(m.base);
}
int main(int argc,char **argv) {
    if(argc>1) { patch_checks(argv[1]); goto done; }
    execute_veneer();
    for(unsigned kind=1;kind<=4;kind++) {
        reset(); runtime_mouse_camera_move(10,20); native_mouse_hook(&frame,kind);
        uintptr_t yaw=kind==1 ? object(5)+0x414 : kind==2 ? object(2)+0x414 : kind==3 ? object(0)+0xbc : frame.x[29]-0x44;
        uintptr_t pitch=kind==1 ? object(1)+0x108 : kind==2 ? object(0)+0x15c : kind==3 ? object(0)+0xa4 : object(0)+0x50;
        near(native_mouse_float(yaw),-1); near(native_mouse_float(pitch),-2);
        native_mouse_hook(&frame,kind); near(native_mouse_float(yaw),-1); /* consumed once */
        if(kind==2) near(native_mouse_float(object(0)+0x154),-2);
        if(kind==4) near(native_mouse_float(object(0)+0x160),-2);
    }
    reset(); runtime_mouse_camera_move(100000,100000); native_mouse_hook(&frame,1);
    near(native_mouse_float(object(1)+0x108),-80); /* pitch clamp; yaw stays wrapped */
    check(fabsf(native_mouse_float(object(5)+0x414))<=180,"yaw wrapped");
    reset(); uint32_t recall=1<<8; memcpy((void *)(object(3)+0x15c),&recall,4);
    runtime_mouse_camera_move(10,0); native_mouse_hook(&frame,4);
    near(native_mouse_float(object(0)+0x54),-1); near(native_mouse_float(frame.x[29]-0x44),0);
    reset(); runtime_mouse_camera_set(1,100,1); runtime_mouse_camera_move(.25,.5); native_mouse_hook(&frame,1);
    near(native_mouse_float(object(5)+0x414),-.05); near(native_mouse_float(object(1)+0x108),.1);
    reset(); now+=1000000000; runtime_mouse_camera_move(10,0); native_mouse_hook(&frame,1);
    near(native_mouse_float(object(5)+0x414),-1); /* first move after idle retained */
    reset(); runtime_mouse_camera_move(10,0); now+=126000000; native_mouse_hook(&frame,1);
    near(native_mouse_float(object(5)+0x414),0); /* genuinely old queued input discarded */
    reset(); runtime_mouse_camera_move(10,0); runtime_mouse_camera_clear(); native_mouse_hook(&frame,1);
    near(native_mouse_float(object(5)+0x414),0); near(native_mouse_float(object(5)+0x420),0);
    runtime_mouse_camera_move(NAN,0); runtime_mouse_camera_move(0,INFINITY); native_mouse_hook(&frame,1);
    near(native_mouse_float(object(5)+0x414),0);
    runtime_mouse_camera_set(0,50,0); runtime_mouse_camera_move(10,0); native_mouse_hook(&frame,1);
    near(native_mouse_float(object(5)+0x414),0);
    reset(); native_mouse_store(object(5)+0x420,.8f); runtime_mouse_camera_move(10,0); native_mouse_hook(&frame,1);
    near(native_mouse_float(object(5)+0x420),.8f); /* controller input preserved */
    reset(); pthread_t workers[4];
    for(unsigned i=0;i<4;i++) check(!pthread_create(workers+i,NULL,queue_movement,NULL),"start producer");
    for(unsigned i=0;i<4;i++) check(!pthread_join(workers[i],NULL),"join producer");
    native_mouse_hook(&frame,1);
    near(native_mouse_float(object(5)+0x414),-20); near(native_mouse_float(object(1)+0x108),-40);
done:
    printf("{\"status\":\"pass\",\"checked\":%u}\n",checked); return 0;
}
