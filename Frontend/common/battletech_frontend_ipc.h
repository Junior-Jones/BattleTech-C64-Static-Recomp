#ifndef BATTLETECH_FRONTEND_IPC_H
#define BATTLETECH_FRONTEND_IPC_H

#ifdef _WIN32
#include <windows.h>

typedef struct bt_frontend_audio_config {
    volatile LONG enabled;
    volatile LONG volume;
    volatile LONG latency_ms;
} bt_frontend_audio_config;

#endif
#endif
