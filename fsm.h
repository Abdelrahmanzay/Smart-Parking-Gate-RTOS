#ifndef FSM_H
#define FSM_H

#include <stdbool.h>

#include "events.h"

typedef struct {
    GateState_t state;
    bool auto_mode;
} GateFsm_t;

void GateFSM_Init(GateFsm_t *fsm, GateState_t initial_state);
GateState_t GateFSM_HandleEvent(GateFsm_t *fsm, const GateEvent_t *event);

const char *GateFSM_StateName(GateState_t state);
const char *GateFSM_EventName(GateEventType_t event);
const char *GateFSM_SourceName(GateSource_t source);

#endif
