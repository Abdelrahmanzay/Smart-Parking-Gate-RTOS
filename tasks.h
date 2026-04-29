#ifndef TASKS_H
#define TASKS_H

#include <stdbool.h>

#include "events.h"

void Tasks_Init(GateState_t initial_state);
bool Tasks_GetGateState(GateState_t *state);

#endif
