#include "hidway_input_state.h"
#include "hidway_input_source.h"
#include <string.h>
unsigned hidway_origin_slot(unsigned source)
{
    return source == HIDWAY_SOURCE_VIRTUAL ? 2u : source == HIDWAY_SOURCE_TOUCHPAD ? 1u : 0u;
}
void hidway_input_key(hidway_input_state_t *s, unsigned source, unsigned key, uint16_t binding,
                      bool up)
{
    if (key > 255)
        return;
    uint16_t *b = &s->binding[hidway_origin_slot(source)][key];
    if (up)
        *b = 0;
    else if (!*b)
        *b = binding; /* retain DOWN binding across layout/mod changes */
}
void hidway_input_button(hidway_input_state_t *s, unsigned source, uint8_t bit, bool up)
{
    uint8_t *b = &s->mouse[hidway_origin_slot(source)];
    if (up)
        *b &= (uint8_t)~bit;
    else
        *b |= bit;
}
void hidway_input_merge(const hidway_input_state_t *s, hidway_held_t *out)
{
    memset(out, 0, sizeof *out);
    for (unsigned o = 0; o < HIDWAY_INPUT_ORIGINS; o++) {
        uint8_t aliases = 0;
        for (unsigned k = 0; k < 256; k++) {
            unsigned b = s->binding[o][k];
            if (b & HIDWAY_BIND_MOUSE)
                aliases |= (uint8_t)b;
            else if (b & HIDWAY_BIND_MOD)
                out->mods |= (uint8_t)b;
            else if (b)
                hidway_bitmap_set(out->keys, (uint8_t)b);
        }
        out->buttons |= s->mouse[o];
        out->aliases |= aliases;
        out->source_buttons[o] = s->mouse[o] | aliases;
    }
}
bool hidway_key_queue_push(hidway_key_queue_t *q, hidway_key_frame_t f)
{
    const hidway_key_frame_t *last =
        q->count ? &q->pending[(q->head + q->count - 1) % HIDWAY_KEY_QUEUE_CAPACITY] : &q->visible;
    if (!memcmp(last, &f, sizeof f))
        return true;
    if (q->count == HIDWAY_KEY_QUEUE_CAPACITY)
        return false;
    q->pending[(q->head + q->count) % HIDWAY_KEY_QUEUE_CAPACITY] = f;
    q->count++;
    return true;
}
hidway_key_frame_t hidway_key_queue_sample(hidway_key_queue_t *q, uint32_t now)
{
    if (q->count && (!q->pacing || (uint32_t)(now - q->shown_at) >= HIDWAY_KEY_DWELL_MS)) {
        q->visible = q->pending[q->head];
        q->head = (q->head + 1) % HIDWAY_KEY_QUEUE_CAPACITY;
        q->count--;
        q->shown_at = now;
        q->pacing = true;
    }
    return q->visible;
}
static bool due(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }
void hidway_pulse_input(hidway_pulse_t *p, bool down, uint32_t now)
{
    if (p->input == down)
        return;
    p->input = down;
    if (!down) {
        if (p->active)
            p->release = true;
        return;
    }
    if (p->active || (p->cooldown && !due(now, p->next_allowed))) {
        p->limited++;
        return;
    }
    p->active = true;
    p->sampled = p->release = false;
}
bool hidway_pulse_sample(hidway_pulse_t *p, uint32_t now, unsigned cps, bool repeat)
{
    unsigned period = cps ? 1000u / cps : 32u;
    if (period < 32u)
        period = 32u;
    unsigned phase = repeat ? period / 2u : 16u;
    if (p->active) {
        if (!p->sampled) {
            p->sampled = true;
            p->shown_at = now;
            return true;
        }
        uint32_t elapsed = now - p->shown_at;
        if (elapsed >= 16u && (p->release || !p->input || (repeat && elapsed >= phase))) {
            uint32_t earliest = p->shown_at + period;
            uint32_t off = now + ((repeat && elapsed > period) ? phase : 16u);
            p->next_allowed = due(off, earliest) ? off : earliest;
            p->active = p->sampled = p->release = false;
            p->cooldown = true;
            return false;
        }
        return true;
    }
    if (p->input && (!p->cooldown || due(now, p->next_allowed))) {
        p->active = p->sampled = true;
        p->release = false;
        p->shown_at = now;
        return true;
    }
    return false;
}

void hidway_pulse_release(hidway_pulse_t *p, uint32_t now, unsigned cps)
{
    p->input = false;
    if (!p->active) {
        p->release = false;
        return;
    }
    unsigned period = cps ? 1000u / cps : 32u;
    if (period < 32u)
        period = 32u;
    uint32_t earliest = p->sampled ? p->shown_at + period : now + period, off = now + 16u;
    if (!p->sampled)
        p->limited++;
    p->next_allowed = due(off, earliest) ? off : earliest;
    p->active = p->sampled = p->release = false;
    p->cooldown = true;
}
static bool same_held(hidway_event_frame_t a, hidway_event_frame_t b)
{
    return a.buttons == b.buttons && a.keyboard.mods == b.keyboard.mods &&
           !memcmp(a.keyboard.keys, b.keyboard.keys, sizeof a.keyboard.keys);
}
bool hidway_event_push(hidway_event_queue_t *q, hidway_event_frame_t f, uint32_t now)
{
    hidway_event_frame_t last =
        q->count ? q->pending[(q->head + q->count - 1) % HIDWAY_EVENT_CAPACITY] : q->visible;
    if (same_held(last, f))
        return true;
    if (q->count == HIDWAY_EVENT_CAPACITY)
        return false;
    unsigned at = (q->head + q->count) % HIDWAY_EVENT_CAPACITY;
    q->pending[at] = f;
    q->created[at] = now;
    q->count++;
    return true;
}
unsigned hidway_event_backlog_ms(const hidway_event_queue_t *q)
{
    unsigned ms = 0;
    uint8_t buttons = q->visible.buttons;
    for (unsigned i = 0; i < q->count; i++) {
        uint8_t next = q->pending[(q->head + i) % HIDWAY_EVENT_CAPACITY].buttons;
        ms += buttons != next ? 16u : 8u;
        buttons = next;
    }
    return ms;
}
bool hidway_event_sample(hidway_event_queue_t *q, hidway_event_frame_t live, uint32_t now,
                         hidway_event_frame_t *out)
{
    if (q->count) {
        if ((uint32_t)(now - q->created[q->head]) >= HIDWAY_EVENT_MAX_AGE_MS)
            return false;
        unsigned dwell = q->pending[q->head].buttons != q->visible.buttons ? 16u : 8u;
        if (!q->pacing || (uint32_t)(now - q->shown_at) >= dwell) {
            q->visible = q->pending[q->head];
            q->head = (q->head + 1) % HIDWAY_EVENT_CAPACITY;
            q->count--;
            q->shown_at = now;
            q->pacing = true;
            *out = q->visible;
            return true;
        }
        *out = q->visible;
        return true;
    }
    live.keyboard = q->visible.keyboard;
    live.buttons = q->visible.buttons;
    *out = live;
    return true;
}
