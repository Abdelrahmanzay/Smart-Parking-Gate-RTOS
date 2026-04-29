#include "tasks.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "adc.h"
#include "fsm.h"
#include "gpio.h"
#include "uart.h"

#define INPUT_POLL_MS           20U
#define SAFETY_POLL_MS          20U
#define LED_POLL_MS             20U
#define DEBOUNCE_STABLE_TICKS   2U
#define AUTO_TAP_MS             300U
#define REVERSE_TIME_MS         500U

#define EVENT_QUEUE_LENGTH      32U
#define LOG_QUEUE_LENGTH        32U

#define STACK_SMALL             160U
#define STACK_MEDIUM            220U

#define PRIORITY_STATUS         (tskIDLE_PRIORITY + 1U)
#define PRIORITY_GATE           (tskIDLE_PRIORITY + 3U)
#define PRIORITY_LED            (tskIDLE_PRIORITY + 3U)
#define PRIORITY_INPUT          (tskIDLE_PRIORITY + 4U)
#define PRIORITY_SAFETY         (tskIDLE_PRIORITY + 5U)

typedef enum {
    BUTTON_DRIVER_OPEN = 0,
    BUTTON_DRIVER_CLOSE,
    BUTTON_SECURITY_OPEN,
    BUTTON_SECURITY_CLOSE,
    BUTTON_OPEN_LIMIT,
    BUTTON_CLOSED_LIMIT,
    BUTTON_COUNT
} ButtonId_t;

typedef struct {
    bool stable;
    bool raw_last;
    uint8_t stable_ticks;
} DebouncedInput_t;

typedef enum {
    CMD_NONE = 0,
    CMD_OPEN,
    CMD_CLOSE,
    CMD_CONFLICT
} CommandKind_t;

typedef struct {
    CommandKind_t kind;
    GateSource_t source;
} EffectiveCommand_t;

typedef enum {
    LOG_EVENT = 0,
    LOG_STATE,
    LOG_OBSTACLE,
    LOG_TEXT
} LogType_t;

typedef struct {
    LogType_t type;
    GateEventType_t event;
    GateState_t state;
    GateSource_t source;
    uint32_t value;
    const char *text;
} LogMessage_t;

static QueueHandle_t gate_event_queue;
static QueueHandle_t log_queue;
static SemaphoreHandle_t gate_state_mutex;
static SemaphoreHandle_t open_limit_sem;
static SemaphoreHandle_t closed_limit_sem;

static GateFsm_t gate_fsm;
static GateState_t shared_gate_state;

static void InputTask(void *argument);
static void GateControlTask(void *argument);
static void LedTask(void *argument);
static void SafetyTask(void *argument);
static void StatusTask(void *argument);

static void LogSend(const LogMessage_t *message)
{
    if (log_queue != 0) {
        (void)xQueueSend(log_queue, message, 0U);
    }
}

static void LogText(const char *text)
{
    LogMessage_t message;

    message.type = LOG_TEXT;
    message.event = GATE_EVENT_NONE;
    message.state = GATE_STATE_STOPPED_MIDWAY;
    message.source = GATE_SOURCE_SYSTEM;
    message.value = 0U;
    message.text = text;

    LogSend(&message);
}

static void LogEvent(const GateEvent_t *event)
{
    LogMessage_t message;

    message.type = LOG_EVENT;
    message.event = event->type;
    message.state = GATE_STATE_STOPPED_MIDWAY;
    message.source = event->source;
    message.value = event->duration_ms;
    message.text = 0;

    LogSend(&message);
}

static void LogState(GateState_t state)
{
    LogMessage_t message;

    message.type = LOG_STATE;
    message.event = GATE_EVENT_NONE;
    message.state = state;
    message.source = GATE_SOURCE_SYSTEM;
    message.value = 0U;
    message.text = 0;

    LogSend(&message);
}

static void LogObstacle(uint16_t raw_value)
{
    LogMessage_t message;

    message.type = LOG_OBSTACLE;
    message.event = GATE_EVENT_OBSTACLE;
    message.state = GATE_STATE_STOPPED_MIDWAY;
    message.source = GATE_SOURCE_SYSTEM;
    message.value = raw_value;
    message.text = 0;

    LogSend(&message);
}

static void SharedStateSet(GateState_t state)
{
    if (gate_state_mutex != 0) {
        (void)xSemaphoreTake(gate_state_mutex, portMAX_DELAY);
        shared_gate_state = state;
        (void)xSemaphoreGive(gate_state_mutex);
    } else {
        shared_gate_state = state;
    }
}

bool Tasks_GetGateState(GateState_t *state)
{
    bool ok = false;

    if ((state != 0) && (gate_state_mutex != 0)) {
        if (xSemaphoreTake(gate_state_mutex, portMAX_DELAY) == pdTRUE) {
            *state = shared_gate_state;
            (void)xSemaphoreGive(gate_state_mutex);
            ok = true;
        }
    }

    return ok;
}

static bool DebounceUpdate(DebouncedInput_t *input, bool raw_value)
{
    if (raw_value == input->raw_last) {
        if (input->stable_ticks < DEBOUNCE_STABLE_TICKS) {
            input->stable_ticks++;
        }
    } else {
        input->raw_last = raw_value;
        input->stable_ticks = 0U;
    }

    if ((input->stable_ticks >= DEBOUNCE_STABLE_TICKS) &&
        (input->stable != raw_value)) {
        input->stable = raw_value;
        return true;
    }

    return false;
}

static EffectiveCommand_t DetermineEffectiveCommand(const DebouncedInput_t buttons[])
{
    EffectiveCommand_t command;
    bool driver_open = buttons[BUTTON_DRIVER_OPEN].stable;
    bool driver_close = buttons[BUTTON_DRIVER_CLOSE].stable;
    bool security_open = buttons[BUTTON_SECURITY_OPEN].stable;
    bool security_close = buttons[BUTTON_SECURITY_CLOSE].stable;

    command.kind = CMD_NONE;
    command.source = GATE_SOURCE_SYSTEM;

    if (security_open && security_close) {
        command.kind = CMD_CONFLICT;
        command.source = GATE_SOURCE_SECURITY;
    } else if (security_open) {
        command.kind = CMD_OPEN;
        command.source = GATE_SOURCE_SECURITY;
    } else if (security_close) {
        command.kind = CMD_CLOSE;
        command.source = GATE_SOURCE_SECURITY;
    } else if (driver_open && driver_close) {
        command.kind = CMD_CONFLICT;
        command.source = GATE_SOURCE_DRIVER;
    } else if (driver_open) {
        command.kind = CMD_OPEN;
        command.source = GATE_SOURCE_DRIVER;
    } else if (driver_close) {
        command.kind = CMD_CLOSE;
        command.source = GATE_SOURCE_DRIVER;
    }

    return command;
}

static bool CommandEquals(EffectiveCommand_t left, EffectiveCommand_t right)
{
    return (left.kind == right.kind) && (left.source == right.source);
}

static bool IsMotionCommand(EffectiveCommand_t command)
{
    return (command.kind == CMD_OPEN) || (command.kind == CMD_CLOSE);
}

static bool IsSecurityCommand(EffectiveCommand_t command)
{
    return (command.source == GATE_SOURCE_SECURITY) &&
           (command.kind != CMD_NONE);
}

static bool IsDriverCommand(EffectiveCommand_t command)
{
    return (command.source == GATE_SOURCE_DRIVER) &&
           (command.kind != CMD_NONE);
}

static uint32_t ElapsedMs(TickType_t start_tick, TickType_t end_tick)
{
    TickType_t elapsed = end_tick - start_tick;
    return (uint32_t)(elapsed * portTICK_PERIOD_MS);
}

static void SendGateEvent(const GateEvent_t *event, TickType_t wait_ticks)
{
    if (xQueueSend(gate_event_queue, event, wait_ticks) == pdTRUE) {
        LogEvent(event);
    } else {
        LogText("Gate event queue full");
    }
}

static void SendGateEventToFront(const GateEvent_t *event)
{
    if (xQueueSendToFront(gate_event_queue, event, 0U) == pdTRUE) {
        LogEvent(event);
    } else {
        LogText("Gate event queue full");
    }
}

static void SendLimitEvent(GateEventType_t type)
{
    GateEvent_t event;

    event.type = type;
    event.source = GATE_SOURCE_SYSTEM;
    event.duration_ms = 0U;
    event.adc_value = 0U;

    if (type == GATE_EVENT_OPEN_LIMIT) {
        (void)xSemaphoreGive(open_limit_sem);
    } else {
        (void)xSemaphoreGive(closed_limit_sem);
    }

    SendGateEvent(&event, pdMS_TO_TICKS(5U));
}

static void SendPressEvent(EffectiveCommand_t command)
{
    GateEvent_t event;

    event.type = (command.kind == CMD_OPEN) ?
                 GATE_EVENT_OPEN_PRESS : GATE_EVENT_CLOSE_PRESS;
    event.source = command.source;
    event.duration_ms = 0U;
    event.adc_value = 0U;

    SendGateEvent(&event, pdMS_TO_TICKS(5U));
}

static void SendReleaseEvent(EffectiveCommand_t command, uint32_t duration_ms)
{
    GateEvent_t event;
    bool short_press = duration_ms <= AUTO_TAP_MS;

    if (command.kind == CMD_OPEN) {
        event.type = short_press ? GATE_EVENT_OPEN_RELEASE_SHORT :
                                   GATE_EVENT_OPEN_RELEASE_HOLD;
    } else {
        event.type = short_press ? GATE_EVENT_CLOSE_RELEASE_SHORT :
                                   GATE_EVENT_CLOSE_RELEASE_HOLD;
    }

    event.source = command.source;
    event.duration_ms = duration_ms;
    event.adc_value = 0U;

    SendGateEvent(&event, pdMS_TO_TICKS(5U));
}

static void SendConflictEvent(GateSource_t source)
{
    GateEvent_t event;

    event.type = GATE_EVENT_CONFLICT;
    event.source = source;
    event.duration_ms = 0U;
    event.adc_value = 0U;

    SendGateEvent(&event, pdMS_TO_TICKS(5U));
}

static void InputTask(void *argument)
{
    DebouncedInput_t buttons[BUTTON_COUNT] = {0};
    EffectiveCommand_t current_command;
    bool driver_lockout = false;
    TickType_t command_start_tick = xTaskGetTickCount();

    (void)argument;

    current_command.kind = CMD_NONE;
    current_command.source = GATE_SOURCE_SYSTEM;

    for (;;) {
        bool changed[BUTTON_COUNT];
        bool driver_active;
        EffectiveCommand_t next_command;
        EffectiveCommand_t raw_command;

        changed[BUTTON_DRIVER_OPEN] =
            DebounceUpdate(&buttons[BUTTON_DRIVER_OPEN], GPIO_DriverOpenPressed());
        changed[BUTTON_DRIVER_CLOSE] =
            DebounceUpdate(&buttons[BUTTON_DRIVER_CLOSE], GPIO_DriverClosePressed());
        changed[BUTTON_SECURITY_OPEN] =
            DebounceUpdate(&buttons[BUTTON_SECURITY_OPEN], GPIO_SecurityOpenPressed());
        changed[BUTTON_SECURITY_CLOSE] =
            DebounceUpdate(&buttons[BUTTON_SECURITY_CLOSE], GPIO_SecurityClosePressed());
        changed[BUTTON_OPEN_LIMIT] =
            DebounceUpdate(&buttons[BUTTON_OPEN_LIMIT], GPIO_OpenLimitPressed());
        changed[BUTTON_CLOSED_LIMIT] =
            DebounceUpdate(&buttons[BUTTON_CLOSED_LIMIT], GPIO_ClosedLimitPressed());

        if (changed[BUTTON_OPEN_LIMIT] && buttons[BUTTON_OPEN_LIMIT].stable) {
            SendLimitEvent(GATE_EVENT_OPEN_LIMIT);
        }
        if (changed[BUTTON_CLOSED_LIMIT] && buttons[BUTTON_CLOSED_LIMIT].stable) {
            SendLimitEvent(GATE_EVENT_CLOSED_LIMIT);
        }

        raw_command = DetermineEffectiveCommand(buttons);
        driver_active = buttons[BUTTON_DRIVER_OPEN].stable ||
                        buttons[BUTTON_DRIVER_CLOSE].stable;

        /*
         * The TExaS PB switches are momentary. If a security command is pressed
         * while a driver button is held, keep the driver suppressed until the
         * driver releases. This makes "security overrides driver" observable
         * even though PB0/PB1 auto-release in the simulator.
         */
        if (driver_active && IsSecurityCommand(raw_command)) {
            driver_lockout = true;
        } else if (!driver_active) {
            driver_lockout = false;
        }

        next_command = raw_command;
        if (driver_lockout && IsDriverCommand(raw_command)) {
            next_command.kind = CMD_NONE;
            next_command.source = GATE_SOURCE_SYSTEM;
        }

        if (!CommandEquals(next_command, current_command)) {
            TickType_t now = xTaskGetTickCount();

            if ((next_command.kind == CMD_NONE) &&
                IsMotionCommand(current_command)) {
                SendReleaseEvent(current_command,
                                 ElapsedMs(command_start_tick, now));
            } else if (next_command.kind == CMD_CONFLICT) {
                SendConflictEvent(next_command.source);
            } else if (IsMotionCommand(next_command)) {
                SendPressEvent(next_command);
                command_start_tick = now;
            }

            current_command = next_command;
            if (IsMotionCommand(current_command)) {
                command_start_tick = now;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(INPUT_POLL_MS));
    }
}

static void GateControlTask(void *argument)
{
    (void)argument;

    LogState(gate_fsm.state);

    for (;;) {
        GateEvent_t event;

        if (xQueueReceive(gate_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            GateState_t before = gate_fsm.state;

            if (event.type == GATE_EVENT_OPEN_LIMIT) {
                (void)xSemaphoreTake(open_limit_sem, 0U);
            } else if (event.type == GATE_EVENT_CLOSED_LIMIT) {
                (void)xSemaphoreTake(closed_limit_sem, 0U);
            }

            (void)GateFSM_HandleEvent(&gate_fsm, &event);

            if (gate_fsm.state != before) {
                SharedStateSet(gate_fsm.state);
                LogState(gate_fsm.state);
            }

            if ((event.type == GATE_EVENT_OBSTACLE) &&
                (before == GATE_STATE_CLOSING) &&
                (gate_fsm.state == GATE_STATE_REVERSING)) {
                GateEvent_t timeout_event;

                vTaskDelay(pdMS_TO_TICKS(REVERSE_TIME_MS));

                timeout_event.type = GATE_EVENT_REVERSE_TIMEOUT;
                timeout_event.source = GATE_SOURCE_SYSTEM;
                timeout_event.duration_ms = REVERSE_TIME_MS;
                timeout_event.adc_value = 0U;

                LogEvent(&timeout_event);
                before = gate_fsm.state;
                (void)GateFSM_HandleEvent(&gate_fsm, &timeout_event);

                if (gate_fsm.state != before) {
                    SharedStateSet(gate_fsm.state);
                    LogState(gate_fsm.state);
                }
            }
        }
    }
}

static void LedTask(void *argument)
{
    (void)argument;

    for (;;) {
        GateState_t state;
        bool opening = false;
        bool closing = false;

        if (Tasks_GetGateState(&state)) {
            opening = (state == GATE_STATE_OPENING) ||
                      (state == GATE_STATE_REVERSING);
            closing = (state == GATE_STATE_CLOSING);
        }

        GPIO_SetMotionLeds(opening, closing);
        vTaskDelay(pdMS_TO_TICKS(LED_POLL_MS));
    }
}

static void SafetyTask(void *argument)
{
    bool obstacle_latched = false;

    (void)argument;

    for (;;) {
        uint16_t adc_value;
        bool obstacle = ADC0_IsObstacle(&adc_value);
        GateState_t state;

        if (Tasks_GetGateState(&state)) {
            if (obstacle && (state == GATE_STATE_CLOSING) && !obstacle_latched) {
                GateEvent_t event;

                event.type = GATE_EVENT_OBSTACLE;
                event.source = GATE_SOURCE_SYSTEM;
                event.duration_ms = 0U;
                event.adc_value = adc_value;

                LogObstacle(adc_value);
                SendGateEventToFront(&event);
                obstacle_latched = true;
            }

            if (!obstacle || (state != GATE_STATE_CLOSING)) {
                obstacle_latched = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SAFETY_POLL_MS));
    }
}

static void StatusTask(void *argument)
{
    (void)argument;

    UART0_WriteLine("Smart Parking Gate RTOS started");

    for (;;) {
        LogMessage_t message;

        if (xQueueReceive(log_queue, &message, portMAX_DELAY) == pdTRUE) {
            switch (message.type) {
            case LOG_EVENT:
                UART0_WriteString("Event: ");
                UART0_WriteString(GateFSM_EventName(message.event));
                UART0_WriteString(" Source: ");
                UART0_WriteString(GateFSM_SourceName(message.source));
                if (message.value > 0U) {
                    UART0_WriteString(" DurationMs: ");
                    UART0_WriteUInt(message.value);
                }
                UART0_WriteString("\r\n");
                break;

            case LOG_STATE:
                UART0_WriteString("State: ");
                UART0_WriteLine(GateFSM_StateName(message.state));
                break;

            case LOG_OBSTACLE:
                UART0_WriteString("Obstacle ADC: ");
                UART0_WriteUInt(message.value);
                UART0_WriteString("\r\n");
                break;

            case LOG_TEXT:
            default:
                UART0_WriteLine(message.text);
                break;
            }
        }
    }
}

void Tasks_Init(GateState_t initial_state)
{
    BaseType_t ok;

    gate_event_queue = xQueueCreate(EVENT_QUEUE_LENGTH, sizeof(GateEvent_t));
    log_queue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(LogMessage_t));
    gate_state_mutex = xSemaphoreCreateMutex();
    open_limit_sem = xSemaphoreCreateBinary();
    closed_limit_sem = xSemaphoreCreateBinary();

    configASSERT(gate_event_queue != 0);
    configASSERT(log_queue != 0);
    configASSERT(gate_state_mutex != 0);
    configASSERT(open_limit_sem != 0);
    configASSERT(closed_limit_sem != 0);

    GateFSM_Init(&gate_fsm, initial_state);
    shared_gate_state = initial_state;

    ok = xTaskCreate(SafetyTask, "Safety", STACK_SMALL, 0,
                     PRIORITY_SAFETY, 0);
    configASSERT(ok == pdPASS);

    ok = xTaskCreate(InputTask, "Input", STACK_MEDIUM, 0,
                     PRIORITY_INPUT, 0);
    configASSERT(ok == pdPASS);

    ok = xTaskCreate(GateControlTask, "Gate", STACK_MEDIUM, 0,
                     PRIORITY_GATE, 0);
    configASSERT(ok == pdPASS);

    ok = xTaskCreate(LedTask, "LED", STACK_SMALL, 0,
                     PRIORITY_LED, 0);
    configASSERT(ok == pdPASS);

    ok = xTaskCreate(StatusTask, "Status", STACK_MEDIUM, 0,
                     PRIORITY_STATUS, 0);
    configASSERT(ok == pdPASS);
}
