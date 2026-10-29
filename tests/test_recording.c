#include "hidway_recording.h"
#undef NDEBUG
#include <assert.h>
#include <string.h>
static void reject(const char *text){FILE *f=tmpfile();assert(f);fputs(text,f);rewind(f);hw_recording r;char error[128];assert(!hw_rec_load(f,&r,error,sizeof error));assert(!r.events);fclose(f);}
int main(void)
{
    FILE *f=tmpfile();assert(f);hw_rec_screen s={1920,1080,3840,1080};assert(hw_rec_header(f,&s));
    hw_rec_event e={10,1,HW_REC_KEY_DOWN,30,0,0,0,65};assert(hw_rec_write(f,&e));e.type=HW_REC_KEY_UP;e.ms=20;assert(hw_rec_write(f,&e));
    e=(hw_rec_event){25,2,HW_REC_WHEEL,0,0,-120,0,0};assert(hw_rec_write(f,&e));
    e=(hw_rec_event){30,2,HW_REC_BUTTON_DOWN,2,0,0,0,0};assert(hw_rec_write(f,&e));assert(hw_rec_footer(f,4,40));rewind(f);
    hw_recording r;char error[128];assert(hw_rec_load(f,&r,error,sizeof error));assert(r.count==4&&r.duration==40&&r.events[2].type!=r.events[3].type&&r.events[2].x==-120);hw_rec_free(&r);fclose(f);
    reject("HIDWAY_RECORDING\t2\t1\t1\t1\t1\n");
    reject("HIDWAY_RECORDING\t1\t1\t1\t1\t1\n10\t0\t1\t30\t0\t0\t0\t65\n");
    reject("HIDWAY_RECORDING\t1\t1\t1\t1\t1\n10\t0\t1\t30\t0\t0\t0\t65\nEND\t2\t10\n");
    reject("HIDWAY_RECORDING\t1\t1\t1\t1\t1\n10\t64\t1\t30\t0\t0\t0\t65\nEND\t1\t10\n");
    reject("HIDWAY_RECORDING\t1\t1\t1\t1\t1\n10\t0\t1\t30\t0\t0\t0\t65\n9\t0\t2\t30\t0\t0\t0\t65\nEND\t2\t10\n");
    reject("HIDWAY_RECORDING\t1\t1\t1\t1\t1\n999999999999999999999999\t0\t1\t30\t0\t0\t0\t65\nEND\t1\t10\n");
    reject("HIDWAY_RECORDING\t1\t1\t1\t1\t1\n1\t0\t1\t30\t0\t0\t0\t65\nEND\t1\t1\nEXTRA\n");
    puts("Recording round-trip, identities, truncation, bounds, ordering and overflow PASS");return 0;
}
