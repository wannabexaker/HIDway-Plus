#ifndef HIDWAY_REPLAY_H
#define HIDWAY_REPLAY_H
#include "hidway_recording.h"
#include "hidway_input_state.h"
typedef struct {
    uint16_t bindings[HW_REC_DEVICES][1024];
    uint8_t buttons[HW_REC_DEVICES],absolute_valid[HW_REC_DEVICES];
    int32_t absolute_x[HW_REC_DEVICES],absolute_y[HW_REC_DEVICES];
    int wheel_rem,pan_rem;
    hidway_event_frame_t frame;
} hidway_replay_state;
int hidway_replay_binding(const hw_rec_event *e,uint16_t *binding);
int hidway_replay_apply(hidway_replay_state *state,const hw_rec_screen *screen,const hw_rec_event *event);
#endif
