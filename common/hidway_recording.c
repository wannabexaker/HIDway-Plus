#include "hidway_recording.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
static int fail(char *s,unsigned n,const char *why){if(n){snprintf(s,n,"%s",why);}return 0;}
int hw_rec_valid(const hw_rec_event *e)
{
    if(e->ms>HW_REC_MAX_MS||e->device>=HW_REC_DEVICES||e->type<1||e->type>9)return 0;
    if(e->type==HW_REC_RELEASE_DEVICE)return !e->code&&!e->flags&&!e->x&&!e->y&&!e->vk;
    if(e->type<=HW_REC_KEY_UP)return e->code<=65535&&e->flags<=6&&!(e->flags&1)&&e->vk<=255&&(!e->x&&!e->y);
    if(e->type<=HW_REC_BUTTON_UP)return e->code<5&&!e->flags&&!e->x&&!e->y&&!e->vk;
    if(e->code||e->vk)return 0;
    if(e->type==HW_REC_ABSOLUTE)return e->flags<=1&&e->x>=0&&e->x<=65535&&e->y>=0&&e->y<=65535;
    if(e->flags)return 0;
    if(e->type==HW_REC_MOVE)return e->x>=-1000000&&e->x<=1000000&&e->y>=-1000000&&e->y<=1000000;
    return e->x>=-32768&&e->x<=32767&&!e->y;
}
int hw_rec_header(FILE *f,const hw_rec_screen *s){return fprintf(f,"HIDWAY_RECORDING\t1\t%u\t%u\t%u\t%u\n",s->width,s->height,s->virtual_width,s->virtual_height)>0;}
int hw_rec_write(FILE *f,const hw_rec_event *e){return hw_rec_valid(e)&&fprintf(f,"%u\t%u\t%u\t%u\t%u\t%d\t%d\t%u\n",e->ms,e->device,e->type,e->code,e->flags,e->x,e->y,e->vk)>0;}
int hw_rec_footer(FILE *f,unsigned count,uint32_t duration){return fprintf(f,"END\t%u\t%u\n",count,duration)>0;}
/* Reject missing/extra fields, overflow, signs on unsigned fields and suffixes. */
static int numbers(char *p,int64_t *v,unsigned n)
{
    for(unsigned i=0;i<n;i++) {
        char *end;errno=0;if(!*p||*p=='+'||*p==' '||*p=='\t')return 0;
        long long x=strtoll(p,&end,10);if(errno||end==p)return 0;v[i]=(int64_t)x;
        if(i+1<n){if(*end!='\t')return 0;p=end+1;}
        else if(strcmp(end,"\n")&&strcmp(end,"\r\n")&&*end)return 0;
    }
    return 1;
}
void hw_rec_free(hw_recording *r){free(r->events);memset(r,0,sizeof *r);}
int hw_rec_load(FILE *f,hw_recording *out,char *error,unsigned capacity)
{
    char line[256];int64_t n[8];hw_recording r={0};unsigned allocated=0;uint32_t last=0;int done=0;
    memset(out,0,sizeof *out);
    if(!fgets(line,sizeof line,f)||strncmp(line,"HIDWAY_RECORDING\t",17)||!numbers(line+17,n,5)||n[0]!=1)return fail(error,capacity,"Unsupported recording header");
    for(unsigned i=1;i<5;i++)if(n[i]<1||n[i]>65535)return fail(error,capacity,"Invalid screen dimensions");
    r.screen.width=(uint32_t)n[1];r.screen.height=(uint32_t)n[2];r.screen.virtual_width=(uint32_t)n[3];r.screen.virtual_height=(uint32_t)n[4];
    while(fgets(line,sizeof line,f)) {
        if(!strncmp(line,"END\t",4)) {
            if(!numbers(line+4,n,2)||n[0]!=r.count||n[1]<last||n[1]>HW_REC_MAX_MS)goto invalid;
            r.duration=(uint32_t)n[1];done=1;break;
        }
        if(r.count>=HW_REC_MAX_EVENTS||!numbers(line,n,8))goto invalid;
        for(unsigned i=0;i<8;i++)if(i!=5&&i!=6&&(n[i]<0||n[i]>UINT32_MAX))goto invalid;
        if(n[5]<INT32_MIN||n[5]>INT32_MAX||n[6]<INT32_MIN||n[6]>INT32_MAX)goto invalid;
        hw_rec_event e={(uint32_t)n[0],(uint32_t)n[1],(uint32_t)n[2],(uint32_t)n[3],(uint32_t)n[4],(int32_t)n[5],(int32_t)n[6],(uint32_t)n[7]};
        if(!hw_rec_valid(&e)||e.ms<last)goto invalid;
        if(r.count==allocated) {
            unsigned next=allocated?allocated*2:1024;if(next>HW_REC_MAX_EVENTS)next=HW_REC_MAX_EVENTS;
            void *mem=realloc(r.events,(size_t)next*sizeof *r.events);if(!mem){hw_rec_free(&r);return fail(error,capacity,"Not enough memory");}r.events=mem;allocated=next;
        }
        last=e.ms;r.events[r.count++]=e;
    }
    if(!done||!r.count||ferror(f)||fgetc(f)!=EOF)goto invalid;
    *out=r;return 1;
invalid:hw_rec_free(&r);return fail(error,capacity,"Incomplete, out-of-order or invalid recording");
}
