#ifndef EVENTS_H
#define EVENTS_H

#include <stdint.h>

typedef enum {
    GATE_STATE_IDLE_OPEN = 0,
    GATE_STATE_IDLE_CLOSED,
    GATE_STATE_OPENING,
    GATE_STATE_CLOSING,
    GATE_STATE_STOPPED_MIDWAY,
    GATE_STATE_REVERSING
} GateState_t;

typedef enum {
    GATE_SOURCE_DRIVER = 0,
    GATE_SOURCE_SECURITY,
    GATE_SOURCE_SYSTEM
} GateSource_t;

typedef enum {
    GATE_EVENT_NONE = 0,
    GATE_EVENT_OPEN_PRESS,
    GATE_EVENT_CLOSE_PRESS,
    GATE_EVENT_OPEN_RELEASE_SHORT,
    GATE_EVENT_CLOSE_RELEASE_SHORT,
    GATE_EVENT_OPEN_RELEASE_HOLD,
    GATE_EVENT_CLOSE_RELEASE_HOLD,
    GATE_EVENT_OPEN_LIMIT,
    GATE_EVENT_CLOSED_LIMIT,
    GATE_EVENT_OBSTACLE,
    GATE_EVENT_CONFLICT,
    GATE_EVENT_REVERSE_TIMEOUT
} GateEventType_t;

typedef struct {
    GateEventType_t type;
    GateSource_t source;
    uint32_t duration_ms;
    uint16_t adc_value;
} GateEvent_t;

#endif
