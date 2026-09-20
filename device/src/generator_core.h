#ifndef KS37_GENERATOR_CORE_H
#define KS37_GENERATOR_CORE_H
#include <stdint.h>

/* Portable pilot kernel only. No device addresses, allocation, MIDI or IRQs.
 * Single-owner API: caller must serialize commands and publication.
 * Pending commands are evaluated into a private projection. CURRENT and SEED
 * change only at gen_boundary; Commit always takes the applied CURRENT and
 * Reset discards the entire previous projection. No bounded command FIFO.
 */
enum { GEN_STEPS = 64, GEN_NOTES = 32 };
enum { GEN_DENSITY = 1, GEN_ROTATION = 2, GEN_MOTION = 4, GEN_EVOLVE = 8,
       GEN_LENGTH = 16, GEN_ACCENT = 32, GEN_RANGE = 64,
       GEN_AUTO_PERIOD = 128, GEN_MODE = 256, GEN_ACCENT_PERIOD = 512,
       GEN_ALL_PARAMS = 1023 };
enum { GEN_MOTIF, GEN_CYCLES, GEN_ARCH, GEN_ACCUMULATE, GEN_ANSWER,
       GEN_BRANCH, GEN_SYNCOPATED, GEN_WALK };
typedef enum { GEN_PARAMS, GEN_VARY, GEN_RESET, GEN_COMMIT, GEN_NEW_SEED } GenAction;
typedef enum { GEN_CHANGED, GEN_UNCHANGED, GEN_EVOLVE_ZERO, GEN_EMPTY, GEN_SINGLE,
               GEN_FIXED } GenReason;
typedef struct {
    uint8_t density, rotation, motion, evolve;
    uint8_t length, accent, range, auto_period, mode;
    uint8_t accent_period;
} GenParams;
typedef struct { uint8_t pitch, velocity; } GenCell;
typedef struct {
    uint32_t rng, composition_seed;
    uint64_t active, known;
    GenParams params;
    uint8_t count, notes[GEN_NOTES];
    GenCell cells[GEN_STEPS];
    uint8_t auto_counter;
    /* Fixed event material, independent of grid length/density. Hidden entries
     * survive n/b edits. Phases name the start of the published CURRENT block. */
    uint8_t motif[GEN_NOTES];
    int8_t accent_cycle[16];
    uint8_t motif_phase, accent_phase;
    int8_t motif_direction, accent_direction;
    uint8_t branch_base[GEN_NOTES], branch_id, branch_valid, branch_strength;
    int8_t accumulator_direction;
    int16_t accumulator;
} GenSnapshot;
typedef struct {
    uint8_t valid, action, changed, changed_steps, reason;
} GenResult;
typedef struct {
    GenResult memory, changed_vary, last_vary;
    uint8_t multiple_variations;
} GenFeedback;
typedef struct {
    GenSnapshot seed, current, next_seed, next_current;
    uint32_t random_seed, revision, pending_count;
    GenFeedback pending_feedback, last_feedback;
    uint8_t pending_suppress_auto;
    /* Reset/new capture own their starting phases: a completed old block must
     * never advance a restored/new seed. Ordinary projections inherit advance. */
    uint8_t pending_phase_reset;
} GenState;

/* Return 0 for invalid input / counter saturation, with no state mutation. */
int gen_init(GenState *, const uint8_t *notes, unsigned count, GenParams, uint32_t rng);
int gen_params(GenState *, GenParams, unsigned mask);
int gen_new_seed(GenState *, const uint8_t *notes, unsigned count);
int gen_command(GenState *, GenAction);
void gen_boundary(GenState *);
/* advance=1 means exactly one completed musical cycle, never a repeated PRE.
 * The playback adapter computes into a private copy and publishes atomically
 * before replacing the applied core. Start/stopped apply use advance=0. */
void gen_boundary_ex(GenState *, unsigned advance);
void gen_cancel(GenState *);
GenResult gen_feedback(const GenFeedback *);
uint16_t gen_euclid(unsigned density, unsigned rotation);
uint64_t gen_euclid_length(unsigned length, unsigned density, unsigned rotation);
int gen_equal(const GenSnapshot *, const GenSnapshot *);

/* Scale matches prototype/control.mjs, including its explicit host range guard.
 * The hardware adapter must feed absolute 0..127 candidates, not CC output. */
typedef struct {
    uint8_t value, physical, previous, physical_valid, previous_valid, latched;
} GenControl;
typedef enum { SCALE_UNCHANGED, SCALE_LATCHED, SCALE_NEAR, SCALE_BASELINE,
    SCALE_SAME, SCALE_ENDPOINT, SCALE_CROSSING, SCALE_GUARD, SCALE_SCALED,
    SCALE_INVALID } GenScaleReason;
void gen_control_init(GenControl *, unsigned value);
int gen_control_reset(GenControl *, unsigned value);
GenScaleReason gen_control_move(GenControl *, unsigned absolute);
#endif
