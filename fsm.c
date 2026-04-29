#include "fsm.h"

static void StartOpening(GateFsm_t *fsm)
{
    if (fsm->state != GATE_STATE_IDLE_OPEN) {
        fsm->state = GATE_STATE_OPENING;
        fsm->auto_mode = false;
    }
}

static void StartClosing(GateFsm_t *fsm)
{
    if (fsm->state != GATE_STATE_IDLE_CLOSED) {
        fsm->state = GATE_STATE_CLOSING;
        fsm->auto_mode = false;
    }
}

static void StopMidwayIfMoving(GateFsm_t *fsm)
{
    if ((fsm->state == GATE_STATE_OPENING) ||
        (fsm->state == GATE_STATE_CLOSING)) {
        fsm->state = GATE_STATE_STOPPED_MIDWAY;
        fsm->auto_mode = false;
    }
}

void GateFSM_Init(GateFsm_t *fsm, GateState_t initial_state)
{
    fsm->state = initial_state;
    fsm->auto_mode = false;
}

GateState_t GateFSM_HandleEvent(GateFsm_t *fsm, const GateEvent_t *event)
{
    if ((fsm->state == GATE_STATE_REVERSING) &&
        (event->type != GATE_EVENT_REVERSE_TIMEOUT)) {
        return fsm->state;
    }

    switch (event->type) {
    case GATE_EVENT_OPEN_PRESS:
        StartOpening(fsm);
        break;

    case GATE_EVENT_CLOSE_PRESS:
        StartClosing(fsm);
        break;

    case GATE_EVENT_OPEN_RELEASE_SHORT:
        if (fsm->state == GATE_STATE_OPENING) {
            fsm->auto_mode = true;
        }
        break;

    case GATE_EVENT_CLOSE_RELEASE_SHORT:
        if (fsm->state == GATE_STATE_CLOSING) {
            fsm->auto_mode = true;
        }
        break;

    case GATE_EVENT_OPEN_RELEASE_HOLD:
        if ((fsm->state == GATE_STATE_OPENING) && !fsm->auto_mode) {
            fsm->state = GATE_STATE_STOPPED_MIDWAY;
        }
        break;

    case GATE_EVENT_CLOSE_RELEASE_HOLD:
        if ((fsm->state == GATE_STATE_CLOSING) && !fsm->auto_mode) {
            fsm->state = GATE_STATE_STOPPED_MIDWAY;
        }
        break;

    case GATE_EVENT_OPEN_LIMIT:
        if (fsm->state == GATE_STATE_OPENING) {
            fsm->state = GATE_STATE_IDLE_OPEN;
            fsm->auto_mode = false;
        }
        break;

    case GATE_EVENT_CLOSED_LIMIT:
        if (fsm->state == GATE_STATE_CLOSING) {
            fsm->state = GATE_STATE_IDLE_CLOSED;
            fsm->auto_mode = false;
        }
        break;

    case GATE_EVENT_OBSTACLE:
        if (fsm->state == GATE_STATE_CLOSING) {
            fsm->state = GATE_STATE_REVERSING;
            fsm->auto_mode = false;
        }
        break;

    case GATE_EVENT_CONFLICT:
        StopMidwayIfMoving(fsm);
        break;

    case GATE_EVENT_REVERSE_TIMEOUT:
        if (fsm->state == GATE_STATE_REVERSING) {
            fsm->state = GATE_STATE_STOPPED_MIDWAY;
            fsm->auto_mode = false;
        }
        break;

    case GATE_EVENT_NONE:
    default:
        break;
    }

    return fsm->state;
}

const char *GateFSM_StateName(GateState_t state)
{
    switch (state) {
    case GATE_STATE_IDLE_OPEN:
        return "IDLE_OPEN";
    case GATE_STATE_IDLE_CLOSED:
        return "IDLE_CLOSED";
    case GATE_STATE_OPENING:
        return "OPENING";
    case GATE_STATE_CLOSING:
        return "CLOSING";
    case GATE_STATE_STOPPED_MIDWAY:
        return "STOPPED_MIDWAY";
    case GATE_STATE_REVERSING:
        return "REVERSING";
    default:
        return "UNKNOWN_STATE";
    }
}

const char *GateFSM_EventName(GateEventType_t event)
{
    switch (event) {
    case GATE_EVENT_OPEN_PRESS:
        return "OPEN_PRESS";
    case GATE_EVENT_CLOSE_PRESS:
        return "CLOSE_PRESS";
    case GATE_EVENT_OPEN_RELEASE_SHORT:
        return "OPEN_RELEASE_SHORT";
    case GATE_EVENT_CLOSE_RELEASE_SHORT:
        return "CLOSE_RELEASE_SHORT";
    case GATE_EVENT_OPEN_RELEASE_HOLD:
        return "OPEN_RELEASE_HOLD";
    case GATE_EVENT_CLOSE_RELEASE_HOLD:
        return "CLOSE_RELEASE_HOLD";
    case GATE_EVENT_OPEN_LIMIT:
        return "OPEN_LIMIT";
    case GATE_EVENT_CLOSED_LIMIT:
        return "CLOSED_LIMIT";
    case GATE_EVENT_OBSTACLE:
        return "OBSTACLE";
    case GATE_EVENT_CONFLICT:
        return "CONFLICT";
    case GATE_EVENT_REVERSE_TIMEOUT:
        return "REVERSE_TIMEOUT";
    case GATE_EVENT_NONE:
    default:
        return "NONE";
    }
}

const char *GateFSM_SourceName(GateSource_t source)
{
    switch (source) {
    case GATE_SOURCE_DRIVER:
        return "DRIVER";
    case GATE_SOURCE_SECURITY:
        return "SECURITY";
    case GATE_SOURCE_SYSTEM:
    default:
        return "SYSTEM";
    }
}
