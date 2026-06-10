/**
 * @file app_fsm.c
 * @brief Implementación de la máquina de estados industrial basada en tablas y contexto aislado.
 * @note Completamente desacoplado del driver físico y temporizaciones del botón de control.
 */

#include "app_fsm.h"
#include "button_api.h"  // Requerido para invocar el cambio de modo del botón en caliente
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_fsm, LOG_LEVEL_INF);

K_THREAD_STACK_DEFINE(motor_stack, STACK_SIZE);

typedef void (*state_handler_t)(app_fsm_t *fsm, system_event_t event);

/* Puntero de Contexto Privado para uso exclusivo de la interrupción del sensor */
static app_fsm_t *p_fsm_context = NULL;

static void handle_state_system_off(app_fsm_t *fsm, system_event_t event);
static void handle_state_banda_running(app_fsm_t *fsm, system_event_t event);
static void handle_state_metal_reject(app_fsm_t *fsm, system_event_t event);
static void handle_state_emergency_stop(app_fsm_t *fsm, system_event_t event);

static const state_handler_t state_table[STATE_MAX_STATES] = {
    [STATE_SYSTEM_OFF]     = handle_state_system_off,
    [STATE_BANDA_RUNNING]  = handle_state_banda_running,
    [STATE_METAL_REJECT]   = handle_state_metal_reject,
    [STATE_EMERGENCY_STOP] = handle_state_emergency_stop,
};

/* --- Callbacks de Tareas Diferidas (Work Queues) de la FSM --- */

static void on_sensor_change(const LJ12A3_Sensor_t *sensor)
{
    ARG_UNUSED(sensor);
    if (p_fsm_context != NULL) {
        k_work_submit(&p_fsm_context->sensor_work);
    }
}

static void sensor_work_handler(struct k_work *work)
{
    app_fsm_t *fsm = CONTAINER_OF(work, app_fsm_t, sensor_work);

    if (sensor_metal_detected(fsm->sensor)) {
        app_fsm_dispatch(fsm, EVENT_METAL_DETECTED);
    } else {
        app_fsm_dispatch(fsm, EVENT_METAL_CLEARED);
    }
}

static void servo_hold_timeout(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    app_fsm_t *fsm = CONTAINER_OF(dwork, app_fsm_t, servo_hold_work);

    app_fsm_dispatch(fsm, EVENT_TIMEOUT);
}

static void emergency_led_blink_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    app_fsm_t *fsm = CONTAINER_OF(dwork, app_fsm_t, emergency_led_work);
    static uint8_t led_state = 0;

    if (fsm->current_state == STATE_EMERGENCY_STOP) {
        led_state = !led_state;
        gpio_pin_set_dt(fsm->user_led, led_state);
        k_work_reschedule(&fsm->emergency_led_work, K_MSEC(EMERGENCY_BLINK_MS));
    }
}

/* --- Hilo de Control y Gestión del Motor (Puente H) --- */

static void motor_manager_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p2); ARG_UNUSED(p3);
    uint8_t last_speed = 0xFF;    
    app_fsm_t *fsm = (app_fsm_t *)p1;

    while (1) {
        k_sem_take(&fsm->sem_motor_update, K_FOREVER);

        if (fsm->motor_speed != last_speed) {
            if (fsm->motor_speed <= MOTOR_MIN_SPEED_PERCENT) {
                l298h_stop(fsm->motor);
                LOG_INF("Motor de la banda detenido de forma segura.");
            } else {
                l298h_set_direction(fsm->motor, MOTOR_DIR);
                l298h_set_speed(fsm->motor, fsm->motor_speed);
                LOG_INF("Velocidad de la banda actualizada al %d%%.", fsm->motor_speed);
            }
            last_speed = fsm->motor_speed;
        }
    }
}

/* --- Inicialización del Módulo --- */

bool app_fsm_init(app_fsm_t *fsm, const L298H_Motor_t *motor, const SG90_Servo_t *servo,
                  const LJ12A3_Sensor_t *sensor, const struct gpio_dt_spec *led, struct button_handler *button)
{
    if (!fsm || !motor || !servo || !sensor || !led || !button) {
        return false;
    }

    p_fsm_context = fsm;

    fsm->motor = motor;
    fsm->servo = servo;
    fsm->sensor = sensor;
    fsm->user_led = led;
    fsm->button = button; // Vinculación del objeto periférico
    fsm->current_state = STATE_SYSTEM_OFF;
    fsm->motor_speed = MOTOR_MIN_SPEED_PERCENT;

    if (!l298h_init(fsm->motor) || !servo_init(fsm->servo)) {
        LOG_ERR("Fallo crítico al inicializar los actuadores físicos del sistema.");
        return false;
    }

    servo_set_angle(fsm->servo, 0); 
    l298h_stop(fsm->motor);        

    k_sem_init(&fsm->sem_motor_update, 0, 1);

    k_work_init(&fsm->sensor_work, sensor_work_handler);
    k_work_init_delayable(&fsm->servo_hold_work, servo_hold_timeout);
    k_work_init_delayable(&fsm->emergency_led_work, emergency_led_blink_handler);

    if (!sensor_init(fsm->sensor, on_sensor_change)) {
        LOG_ERR("Fallo crítico al establecer el enlace con el sensor inductivo.");
        return false;
    }

    k_thread_create(&fsm->motor_thread_data, motor_stack, K_THREAD_STACK_SIZEOF(motor_stack),
                    motor_manager_thread, (void *)fsm, NULL, NULL,
                    K_PRIO_COOP(5), 0, K_NO_WAIT);

    LOG_INF("Módulo de la máquina de estados inicializado correctamente.");
    return true;
}

/* --- Despachador Centralizado de Eventos --- */

void app_fsm_dispatch(app_fsm_t *fsm, system_event_t event)
{
    if (fsm->current_state == STATE_SYSTEM_OFF) {
        servo_set_angle(fsm->servo, SERVO_BASE_ANGLE);
        if (event == EVENT_METAL_DETECTED || event == EVENT_METAL_CLEARED || event == EVENT_TIMEOUT) {
            return; 
        }
    }

    /* Intercepción prioritaria global para el evento de parada de emergencia */
    if (event == EVENT_EMERGENCY) {
        LOG_ERR("[ALERTA] Parada de emergencia solicitada.");
        fsm->motor_speed = 0;
        l298h_stop(fsm->motor);
        servo_set_angle(fsm->servo, 0);
        k_sem_reset(&fsm->sem_motor_update);
        k_work_cancel_delayable(&fsm->servo_hold_work);
        
        /* Ponemos inmediatamente el botón en modo rápido de flanco para permitir el desbloqueo */
        button_api_set_immediate_mode(fsm->button, true);
        
        k_work_reschedule(&fsm->emergency_led_work, K_MSEC(EMERGENCY_BLINK_MS));
        fsm->current_state = STATE_EMERGENCY_STOP;
        return;
    }

    if (fsm->current_state < STATE_MAX_STATES && state_table[fsm->current_state] != NULL) {
        state_table[fsm->current_state](fsm, event);
    }
}

/* --- Implementación de Funciones de Lógica por Estado --- */

static void handle_state_system_off(app_fsm_t *fsm, system_event_t event)
{
    if (event == EVENT_CLICK_INCREMENT) {
        LOG_INF("Arrancando banda transportadora...");
        fsm->motor_speed = MOTOR_MIN_SPEED_PERCENT + MOTOR_SPEED_STEP;
        k_sem_give(&fsm->sem_motor_update);
        fsm->current_state = STATE_BANDA_RUNNING;
    }
}

static void handle_state_banda_running(app_fsm_t *fsm, system_event_t event)
{
    if (event == EVENT_CLICK_INCREMENT) {
        if (fsm->motor_speed + MOTOR_SPEED_STEP <= MOTOR_MAX_SPEED_PERCENT) {
            fsm->motor_speed += MOTOR_SPEED_STEP;
        } else {
            fsm->motor_speed = MOTOR_MAX_SPEED_PERCENT;
        }
        k_sem_give(&fsm->sem_motor_update);
    }
    else if (event == EVENT_CLICK_DECREMENT) {
        if (fsm->motor_speed >= MOTOR_MIN_SPEED_PERCENT + MOTOR_SPEED_STEP) {
            fsm->motor_speed -= MOTOR_SPEED_STEP;
        } else {
            fsm->motor_speed = MOTOR_MIN_SPEED_PERCENT;
        }
        k_sem_give(&fsm->sem_motor_update);

        if (fsm->motor_speed <= MOTOR_MIN_SPEED_PERCENT) {
            LOG_INF("Banda al mínimo operativo. Pasando a estado de apagado.");
            k_work_cancel_delayable(&fsm->servo_hold_work);
            servo_set_angle(fsm->servo, 0);
            fsm->current_state = STATE_SYSTEM_OFF;
        }
    }
    else if (event == EVENT_METAL_DETECTED) {
        LOG_WRN("[DETECCIÓN] Elemento metálico localizado en la Banda.");
        servo_set_angle(fsm->servo, SERVO_DETECTION_ANGLE);
        k_work_schedule(&fsm->servo_hold_work, K_MSEC(SERVO_DETECTION_HOLD_MS));
        fsm->current_state = STATE_METAL_REJECT;
    }
}

static void handle_state_metal_reject(app_fsm_t *fsm, system_event_t event)
{
    if (event == EVENT_TIMEOUT) {
        if (sensor_metal_detected(fsm->sensor)) {
            LOG_WRN("El metal sigue presente en el área de descarga. Re-programando eyección.");
            k_work_schedule(&fsm->servo_hold_work, K_MSEC(SERVO_DETECTION_HOLD_MS));
        } else {
            LOG_INF("Área limpia de material ferroso. Retornando a flujo normal.");
            servo_set_angle(fsm->servo, SERVO_BASE_ANGLE);
            fsm->current_state = STATE_BANDA_RUNNING;
        }
    }
    else if (event == EVENT_CLICK_INCREMENT) {
        if (fsm->motor_speed + MOTOR_SPEED_STEP <= MOTOR_MAX_SPEED_PERCENT) {
            fsm->motor_speed += MOTOR_SPEED_STEP;
        } else {
            fsm->motor_speed = MOTOR_MAX_SPEED_PERCENT;
        }
        k_sem_give(&fsm->sem_motor_update);
    }
    else if (event == EVENT_CLICK_DECREMENT) {
        if (fsm->motor_speed >= MOTOR_MIN_SPEED_PERCENT + MOTOR_SPEED_STEP) {
            fsm->motor_speed -= MOTOR_SPEED_STEP;
        } else {
            fsm->motor_speed = MOTOR_MIN_SPEED_PERCENT;
        }
        
        k_sem_give(&fsm->sem_motor_update);

        if (fsm->motor_speed <= MOTOR_MIN_SPEED_PERCENT) {
            LOG_INF("Banda al mínimo operativo. Pasando a estado de apagado.");
            k_work_cancel_delayable(&fsm->servo_hold_work); 
            servo_set_angle(fsm->servo, 0); 
            fsm->current_state = STATE_SYSTEM_OFF;
        }
    }
}

static void handle_state_emergency_stop(app_fsm_t *fsm, system_event_t event)
{
    /* El evento inmediato del flanco nos despierta aquí al instante */
    if (event == EVENT_BUTTON_PRESSED) {
        LOG_INF("Reinicio del sistema posterior a parada de emergencia aceptado.");
        k_work_cancel_delayable(&fsm->emergency_led_work); 
        
        /* Apagamos el modo inmediato para recuperar la decodificación de clics múltiples */
        button_api_set_immediate_mode(fsm->button, false);
           
        gpio_pin_set_dt(fsm->user_led, 0); 
        fsm->motor_speed = MOTOR_MIN_SPEED_PERCENT;
        servo_set_angle(fsm->servo, SERVO_BASE_ANGLE);
        fsm->current_state = STATE_SYSTEM_OFF;
    }
}