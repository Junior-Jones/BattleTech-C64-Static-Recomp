#ifndef BATTLETECH_C64_SID6581_H
#define BATTLETECH_C64_SID6581_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define BT_SID6581_REG_COUNT 32u
#define BT_SID6581_VOICE_COUNT 3u
#define BT_SID6581_PAL_CLOCK_HZ 985248u
#define BT_SID6581_DEFAULT_SAMPLE_RATE 44100u

typedef enum bt_sid6581_env_state {
    BT_SID6581_ENV_ATTACK = 0,
    BT_SID6581_ENV_DECAY_SUSTAIN = 1,
    BT_SID6581_ENV_RELEASE = 2
} bt_sid6581_env_state;

typedef void (*bt_sid6581_sample_fn)(void *ctx, uint64_t cycle, int16_t sample);

typedef struct bt_sid6581_voice {
    uint32_t accumulator;          /* 24-bit phase accumulator. */
    uint32_t shift_register;       /* 23-bit noise LFSR. */
    uint16_t frequency;
    uint16_t pulse_width;          /* 12-bit. */
    uint16_t rate_counter;         /* ADSR rate counter. */
    uint8_t control;
    uint8_t attack_decay;
    uint8_t sustain_release;
    uint8_t envelope;
    uint8_t exponential_counter;
    uint8_t exponential_period;
    uint8_t shift_pipeline;
    uint8_t msb_rising;
    uint8_t gate;
    bt_sid6581_env_state env_state;
} bt_sid6581_voice;

typedef struct bt_sid6581 {
    uint8_t regs[BT_SID6581_REG_COUNT];
    bt_sid6581_voice voice[BT_SID6581_VOICE_COUNT];
    uint64_t cycle;
    uint64_t emitted_samples;
    uint32_t clock_hz;
    uint32_t sample_rate;
    uint32_t sample_accumulator;
    int32_t filter_low;
    int32_t filter_band;
    int32_t filter_high;
    int32_t last_mixed;
    int16_t last_sample;
    uint8_t bus_latch;
    uint16_t bus_latch_ttl;
    uint8_t failed;
    void *sample_ctx;
    bt_sid6581_sample_fn sample_fn;
} bt_sid6581;

void bt_sid6581_init(bt_sid6581 *sid, uint32_t clock_hz, uint32_t sample_rate,
                     void *sample_ctx, bt_sid6581_sample_fn sample_fn);
void bt_sid6581_reset(bt_sid6581 *sid);
void bt_sid6581_set_sample_sink(bt_sid6581 *sid, void *sample_ctx,
                               bt_sid6581_sample_fn sample_fn);
bool bt_sid6581_tick(bt_sid6581 *sid, uint64_t cycle);
bool bt_sid6581_advance_to(bt_sid6581 *sid, uint64_t cycle);
uint8_t bt_sid6581_read(bt_sid6581 *sid, uint8_t reg);
uint8_t bt_sid6581_peek(const bt_sid6581 *sid, uint8_t reg);
bool bt_sid6581_write_at(bt_sid6581 *sid, uint64_t cycle, uint8_t reg, uint8_t value);

#ifdef __cplusplus
}
#endif
#endif
