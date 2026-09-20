#include "native_admission.h"

/* Byte reads also work with unaligned host fixtures. Hardware callers must
 * protect the whole snapshot, not merely individual bytes/words. */
static uint32_t read_le32(const volatile uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void ks37_native_snapshot_read(Ks37NativeSnapshot *s, const Ks37NativeSources *p) {
    s->u_published_record = p->u[0x04];
    s->u_mode_request = p->u[0x06];
    s->u_record_request = p->u[0x08];
    s->u_record_suppression = p->u[0x0a];
    s->u_chord_mode = p->u[0x0c];
    s->u_chord_held = p->u[0x0d];
    s->u_seq = p->u[0x0f];
    s->u_transport = p->u[0x10];
    s->u_record = p->u[0x12];
    s->u_start_wait = p->u[0x13];
    s->u_page = p->u[0x15];
    s->u_page_timer = read_le32(p->u + 0x18);
    s->u_transport_aux = p->u[0x1c];
    s->c_emit = p->c[0x04];
    s->c_pause = p->c[0x05];
    s->c_clock_selector = p->c[0x0c];
    s->c_length_target = p->c[0x11];
    s->c_rephase_request = p->c[0x12];
    s->c_rephase_target = p->c[0x13];
    s->c_clock_start_arm = p->c[0x27];
    s->c_clock_resume_arm = p->c[0x28];
    s->c_release_deadline = read_le32(p->c + 0x44);
    s->c_division_request = p->c[0x54];
    s->c_division_target = p->c[0x55];
    s->c_block_request = p->c[0x56];
    s->c_block_target = p->c[0x57];
    s->c_rephase_threshold = read_le32(p->c + 0x58);
    s->c_rebuild_request = p->c[0x5d];
    s->c_rebuild_target = p->c[0x5e];
    s->service_mode = p->h[0xb9];
    s->record_held = *p->record_held;
    s->length_pending = *p->length_pending;
    s->capture_started = *p->capture_started;
    s->shift = *p->shift;
}

uint32_t ks37_native_local_rejects(const Ks37NativeSnapshot *s) {
    uint32_t r = 0;
    if (s->u_seq != 1) r |= KS37_REJECT_SEQ;
    if (s->u_page > 1) r |= KS37_REJECT_PAGE;
    if (s->u_transport || s->c_emit || s->c_pause) r |= KS37_REJECT_TRANSPORT;
    if (s->u_record || s->record_held || s->u_record_suppression) r |= KS37_REJECT_RECORD;
    if (s->length_pending) r |= KS37_REJECT_LENGTH;
    if (s->capture_started || s->u_chord_mode > 1) r |= KS37_REJECT_CAPTURE;
    if (s->u_mode_request || s->u_record_request ||
        s->u_published_record != s->u_record || s->u_page_timer ||
        s->u_start_wait || s->u_transport_aux) r |= KS37_REJECT_UI_PENDING;
    if (s->c_length_target != 0xff || s->c_rephase_request ||
        s->c_rephase_target != 0xff || s->c_rephase_threshold ||
        s->c_division_request || s->c_division_target != 0xff ||
        s->c_block_request || s->c_block_target != 0xff ||
        s->c_rebuild_request || s->c_rebuild_target != 0xff)
        r |= KS37_REJECT_CONTROLLER_PENDING;
    if (s->c_release_deadline != UINT32_C(0x7fffffff)) r |= KS37_REJECT_RELEASE_PENDING;
    if (s->service_mode) r |= KS37_REJECT_SERVICE;
    if (s->shift) r |= KS37_REJECT_SHIFT;
    /* C+27 is 1 after full Stop. C+28 can survive Pause -> Stop.
     * Neither arm byte is an independent pending command. */
    return r;
}

void ks37_native_gesture_context(GenGestureContext *g, const Ks37NativeSnapshot *s) {
    uint32_t rejects = ks37_native_local_rejects(s);
    g->known = GG_K_SEQ | GG_K_PAGE | GG_K_RECORD | GG_K_LENGTH | GG_K_CAPTURE;
    g->seq = s->u_seq == 1;
    g->page = s->u_page == 0 ? GG_PAGE_CHORD : (s->u_page == 1 ? GG_PAGE_CC : GG_PAGE_OTHER);
    g->transport = 0;
    if (!(rejects & KS37_REJECT_TRANSPORT)) {
        g->transport = GG_STOPPED;
        g->known |= GG_K_TRANSPORT;
    } else if (s->c_pause && !s->c_emit) {
        g->transport = GG_PAUSED;
        g->known |= GG_K_TRANSPORT;
    } else if (s->c_emit && !s->c_pause) {
        g->transport = GG_PLAYING;
        g->known |= GG_K_TRANSPORT;
    }
    /* U+10==1 can mean waiting for external start, not just pause.
     * Ambiguous waiting/inconsistent states keep TRANSPORT unknown. */
    g->record_idle = !(rejects & KS37_REJECT_RECORD);
    g->length_pending = s->length_pending;
    g->capture = !!(rejects & KS37_REJECT_CAPTURE);
    g->notes_held = 0; /* Ignored: GG_K_NOTES deliberately absent. */
    g->pending = 0; /* Ignored unless a blocker below proves pending/busy. */
    if (rejects & (KS37_REJECT_UI_PENDING | KS37_REJECT_CONTROLLER_PENDING |
                   KS37_REJECT_RELEASE_PENDING | KS37_REJECT_SERVICE | KS37_REJECT_SHIFT)) {
        g->pending = 1;
        g->known |= GG_K_PENDING;
    }
}

Ks37ButtonObservation ks37_button_observe(Ks37ButtonObserver *s,
                                         const volatile uint8_t *b,
                                         unsigned released_sample) {
    unsigned id = b[0x0d], stable = b[0x12];
    if (id > 8 || released_sample > 1 || stable > 1) return KS37_BUTTON_INVALID;
    if (!stable) return KS37_BUTTON_NONE;
    uint16_t mask = (uint16_t)(1u << id);
    unsigned down = !released_sample;
    unsigned seen = (s->seen & mask) != 0;
    unsigned was_down = (s->held & mask) != 0;
    s->seen |= mask;
    if (down) s->held |= mask;
    else s->held &= (uint16_t)~mask;
    if (!seen) return down ? KS37_BUTTON_INITIAL_DOWN : KS37_BUTTON_INITIAL_UP;
    if (down == was_down) return KS37_BUTTON_NONE;
    return down ? KS37_BUTTON_DOWN : KS37_BUTTON_UP;
}
