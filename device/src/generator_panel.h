#ifndef KS37_GENERATOR_PANEL_H
#define KS37_GENERATOR_PANEL_H
#include "generator_controller.h"
/* V4: Main L/d/r/E, Shift n/A/b/t, Shift+Octave O. RATE actual BPM.
 * Idle shows the Mode code. Parameter hints last2000 nominal ticks; commands
 * last1600. LED page changes never interrupt the display. */
enum { GP_STEADY, GP_CAPTURE, GP_CONFIRM };
typedef struct {
    uint32_t notice_tick, parameter_tick, pulse_tick, capture_tick;
    uint32_t stock_color, last_color;
    uint32_t diagnostic_tick;
    uint8_t active, capturing, notice_valid, parameter_valid, pulse_valid;
    uint8_t parameter, notice[3], stock_valid, last_valid;
    uint8_t capture_complete_count;
    uint16_t diagnostic_code;
    uint8_t page, page_valid;
} GenPanelState;
typedef struct {
    uint32_t now, event_tick;
    uint16_t flags;
    uint8_t active, capturing, capture_count, complete_count, action, parameter;
    uint8_t values[GC_CONTROLS], mode, range;
    uint8_t page, page_valid; /* applied CURRENT page0..3 */
    int8_t step; /* compatibility observation; never rendered as step digits */
    uint8_t pending, fault;
    GenResult result;
} GenPanelInput;
typedef struct {
    uint8_t display_owned, glyphs[3], text[3], pending, pattern, brightness, color_owned;
    uint32_t color;
} GenPanelView;
void gp_update(GenPanelState *,const GenPanelInput *);
void gp_view(const GenPanelState *,const GenPanelInput *,GenPanelView *);
/* Caller serializes with update/view. 1..99 owns the inactive display for
 * 5000 ticks; zero clears; larger codes are ignored. An active transition
 * clears it. Ordinary hints neither replace the code nor extend its life. */
void gp_diagnostic(GenPanelState *,unsigned code,uint32_t now);
unsigned gp_capturing(const GenController *);
/* Native writer request updates stock shadow; generated writes bypass it. */
uint32_t gp_stock_pixel(GenPanelState *,const GenPanelView *,uint32_t original);
unsigned gp_output_due(GenPanelState *,const GenPanelView *,uint32_t *color);
#endif
