#include "generator_keyled.h"

void gk_project_steps(GenKeyledInput *i,uint64_t active,unsigned length,int global_step){
    if(!i)return;
    i->hits=0;i->step=-1;i->valid_steps=i->page=0;
    if(!length||length>64)return;
    unsigned start=0;
    if(global_step>=0&&(unsigned)global_step<length){
        i->page=(uint8_t)((unsigned)global_step/16);start=(unsigned)i->page*16;
        i->step=(int8_t)((unsigned)global_step-start);
    }
    unsigned remaining=length-start;
    i->valid_steps=(uint8_t)(remaining<16?remaining:16);
    i->hits=(uint16_t)(active>>start);
    if(i->valid_steps<16)i->hits&=(uint16_t)((1u<<i->valid_steps)-1u);
}

unsigned gk_baseline_ready(const GenKeyledState *s) {
    return s && s->seen[0]==UINT32_MAX && (s->seen[1]&0x1ffu)==0x1ffu;
}
static uint32_t desired(const GenKeyledState *s,const GenKeyledInput *i,
                        unsigned index) {
    if(!i || !i->active || index<1 || index>39 || !gk_baseline_ready(s))
        return s->stock[index];
    /* A failed runtime must not advertise a moving or playable pattern. */
    if(i->fault)return 0;
    if(i->capturing) {
        if(!i->range_valid || i->count>32 || (i->count && !i->notes))return 0;
        for(unsigned n=0;n<i->count;++n) {
            if(i->notes[n]>127)continue;
            int relative=(int)i->notes[n]-(int)i->lower;
            if((index==1 && relative<0) || (index==39 && relative>=GK_KEYS) ||
               (index>=GK_FIRST_KEY && index<GK_FIRST_KEY+GK_KEYS &&
                relative==(int)index-GK_FIRST_KEY))return GK_CAPTURE;
        }
        return 0;
    }
    if(index<GK_FIRST_KEY || index>=GK_FIRST_KEY+GK_STEPS)return 0;
    unsigned step=index-GK_FIRST_KEY;
    if(!i->valid_steps||i->valid_steps>16||step>=i->valid_steps)return 0;
    unsigned hit=(i->hits>>step)&1u;
    if(i->step>=0 && i->step<GK_STEPS && (unsigned)i->step==step)
        return hit?GK_HIT_CURSOR:GK_CURSOR;
    return hit?GK_HIT:0;
}
uint32_t gk_stock_pixel(GenKeyledState *s,const GenKeyledInput *i,
                      unsigned index,uint32_t original) {
    if(!s || index>=GK_PIXELS)return original;
    s->stock[index]=original;s->seen[index>>5]|=1u<<(index&31u);
    uint32_t color=desired(s,i,index);
    s->last[index]=color;return color;
}
unsigned gk_output_due(GenKeyledState *s,const GenKeyledInput *i,
                       unsigned index,uint32_t *color) {
    if(!s || !color || index<1 || index>39 ||
       !(s->seen[index>>5]&(1u<<(index&31u))))return 0;
    uint32_t value=desired(s,i,index);
    if(value==s->last[index])return 0;
    s->last[index]=value;*color=value;return 1;
}
