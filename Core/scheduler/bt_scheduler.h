#ifndef BATTLETECH_C64_BT_SCHEDULER_H
#define BATTLETECH_C64_BT_SCHEDULER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BT_SCHEDULER_MAX_EVENTS 256u
#define BT_SCHEDULER_MAX_TICKERS 16u
typedef void (*bt_scheduler_event_fn)(void *ctx,uint64_t cycle,uint32_t type,uint64_t value);
typedef void (*bt_scheduler_tick_fn)(void *ctx,uint64_t cycle);
typedef struct bt_scheduler_event {uint64_t cycle;uint64_t sequence;uint64_t value;uint32_t type;uint16_t priority;uint8_t active;} bt_scheduler_event;
typedef struct bt_scheduler_ticker {void *ctx;bt_scheduler_tick_fn fn;} bt_scheduler_ticker;
typedef struct bt_scheduler {uint64_t cycle;uint64_t next_sequence;bt_scheduler_event events[BT_SCHEDULER_MAX_EVENTS];size_t ticker_count;bt_scheduler_ticker tickers[BT_SCHEDULER_MAX_TICKERS];void *event_ctx;bt_scheduler_event_fn event_fn;uint8_t failed;} bt_scheduler;
void bt_scheduler_init(bt_scheduler *s,void *event_ctx,bt_scheduler_event_fn event_fn);
bool bt_scheduler_add_ticker(bt_scheduler *s,void *ctx,bt_scheduler_tick_fn fn);
bool bt_scheduler_schedule(bt_scheduler *s,uint64_t cycle,uint16_t priority,uint32_t type,uint64_t value);
bool bt_scheduler_advance_to(bt_scheduler *s,uint64_t cycle);
#ifdef __cplusplus
}
#endif
#endif
