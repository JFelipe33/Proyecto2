/**
 * @file app_fsm.c
 * @brief Implementación de la máquina de estados industrial basada en tablas y contexto aislado.
 */

#include "app_fsm.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* Registro del módulo de logs para diagnóstico asíncrono en tiempo real */
LOG_MODULE_REGISTER(app_fsm, LOG_LEVEL_INF);

/* Espacio reservado en memoria RAM para la pila de ejecución exclusiva del hilo del motor */
K_THREAD_STACK_DEFINE(motor_stack, STACK_SIZE);

/* Definición del tipo puntero a función que estandariza los manejadores de estado */
typedef void (*state_handler_t)(app_fsm_t *fsm, system_event_t event);

/* --- Puntero de Contexto Privado del Módulo --- */
static app_fsm_t *p_fsm_context = NULL;

/* --- Prototipos de los Manejadores de Estado (Lógica Aislada) --- */
static void handle_state_system_off(app_fsm_t *fsm, system_event_t event);
static void handle_state_banda_running(app_fsm_t *fsm, system_event_t event);
static void handle_state_metal_reject(app_fsm_t *fsm, system_event_t event);
static void handle_state_emergency_stop(app_fsm_t *fsm, system_event_t event);

/* Prototipo de la función de enlace del sensor inductivo */
static void on_sensor_change(const LJ12A3_Sensor_t *sensor);

/**
 * @brief Tabla global indexada de estados.
 * Asocia de forma directa cada estado numérico con su respectiva función lógica.
 */
static const state_handler_t state_table[STATE_MAX_STATES] = {
    [STATE_SYSTEM_OFF]     = handle_state_system_off,
    [STATE_BANDA_RUNNING]  = handle_state_banda_running,
    [STATE_METAL_REJECT]   = handle_state_metal_reject,
    [STATE_EMERGENCY_STOP] = handle_state_emergency_stop,
};

/* --- Callbacks de Tareas Diferidas (Work Queues) y Periféricos --- */

static void on_sensor_change(const LJ12A3_Sensor_t *sensor)
{
    /* Evitamos advertencias asociadas al puntero del controlador del sensor */
    ARG_UNUSED(sensor);

    /* Si el contexto de la máquina de estados ha sido registrado correctamente, activamos su tarea */
    if (p_fsm_context != NULL) {
        k_work_submit(&p_fsm_context->sensor_work);
    }
}

static void sensor_work_handler(struct k_work *work)
{
    /* Recuperación segura del contexto original de la estructura contenedora */
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

static void button_window_timeout(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    app_fsm_t *fsm = CONTAINER_OF(dwork, app_fsm_t, button_window_work);

    gpio_pin_set_dt(fsm->user_led, 0);
   
    /* Evaluación protegida del contador de clics empleando la API atómica */
    if (atomic_get(&fsm->click_count) == 1) {
        app_fsm_dispatch(fsm, EVENT_CLICK_INCREMENT);
    }
    atomic_set(&fsm->click_count, 0);
}

static void emergency_hold_timeout(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    app_fsm_t *fsm = CONTAINER_OF(dwork, app_fsm_t, emergency_work);

    k_work_cancel_delayable(&fsm->button_window_work);
    atomic_set(&fsm->click_count, 0);
    app_fsm_dispatch(fsm, EVENT_EMERGENCY);
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

/* --- Filtrado Temporal y Lógica de Entrada Digital --- */

void app_fsm_notify_button_state(app_fsm_t *fsm, bool is_pressed)
{
    uint32_t current_time = k_uptime_get_32();

    /* Bloqueo temporal por software para descarte de señales espurias rápidas */
    if ((current_time - fsm->last_valid_time) < BUTTON_DEBOUNCE_MS) {
        return;
    }
    fsm->last_valid_time = current_time;

    if (is_pressed) {
        app_fsm_dispatch(fsm, EVENT_BUTTON_PRESSED);
        k_work_reschedule(&fsm->emergency_work, K_MSEC(BUTTON_EMERGENCY_HOLD_MS));
        
        /* Incremento seguro en entornos de ejecución concurrentes */
        atomic_inc(&fsm->click_count);
       
        if (atomic_get(&fsm->click_count) == 1) {
            if (fsm->current_state != STATE_EMERGENCY_STOP) {
                gpio_pin_set_dt(fsm->user_led, 1);
            }
            k_work_reschedule(&fsm->button_window_work, K_MSEC(BUTTON_WINDOW_MS));
        }
        else if (atomic_get(&fsm->click_count) == 2) {
            k_work_cancel_delayable(&fsm->button_window_work);
            k_work_cancel_delayable(&fsm->emergency_work);
           
            if (fsm->current_state != STATE_EMERGENCY_STOP) {
                gpio_pin_set_dt(fsm->user_led, 0);
            }
            app_fsm_dispatch(fsm, EVENT_CLICK_DECREMENT);
            atomic_set(&fsm->click_count, 0);
        }
    } else {
        k_work_cancel_delayable(&fsm->emergency_work);
    }
}

/* --- Hilo de Control y Gestión del Motor (Puente H) --- */

static void motor_manager_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p2); ARG_UNUSED(p3);
    uint8_t last_speed = 0xFF;    
    app_fsm_t *fsm = (app_fsm_t *)p1;

    while (1) {
        /* Suspensión eficiente del hilo hasta recibir una orden de actualización */
        k_sem_take(&fsm->sem_motor_update, K_FOREVER);

        if (fsm->motor_speed != last_speed) {
            if (fsm->motor_speed <= MOTOR_MIN_SPEED_PERCENT) {
                l298h_stop(fsm->motor);
                LOG_INF("Motor de la banda detenido de forma segura.");
            } else {
                l298h_set_direction(fsm->motor, L298H_DIR_FORWARD);
                l298h_set_speed(fsm->motor, fsm->motor_speed);
                LOG_INF("Velocidad de la banda actualizada al %d%%.", fsm->motor_speed);
            }
            last_speed = fsm->motor_speed;
        }
    }
}

/* --- Inicialización del Módulo --- */

bool app_fsm_init(app_fsm_t *fsm, const L298H_Motor_t *motor, const SG90_Servo_t *servo,
                  const LJ12A3_Sensor_t *sensor, const struct gpio_dt_spec *led)
{
    if (!fsm || !motor || !servo || !sensor || !led) {
        return false;
    }

    /* Guardamos de forma segura el contexto de la máquina para uso de la interrupción del sensor */
    p_fsm_context = fsm;

    fsm->motor = motor;
    fsm->servo = servo;
    fsm->sensor = sensor;
    fsm->user_led = led;
    fsm->current_state = STATE_SYSTEM_OFF;
    fsm->motor_speed = MOTOR_MIN_SPEED_PERCENT;
    fsm->last_valid_time = 0;

    if (!l298h_init(fsm->motor) || !servo_init(fsm->servo)) {
        LOG_ERR("Fallo crítico al inicializar los actuadores físicos del sistema.");
        return false;
    }

    /* Forzado físico inicial a una posición conocida de seguridad */
    servo_set_angle(fsm->servo, 0); 
    l298h_stop(fsm->motor);        

    /* Inicialización de primitivas de sincronización del Kernel de Zephyr */
    k_sem_init(&fsm->sem_motor_update, 0, 1);
    atomic_set(&fsm->click_count, 0);

    /* Vinculación de los hilos de trabajo asíncronos con el contexto del objeto */
    k_work_init(&fsm->sensor_work, sensor_work_handler);
    k_work_init_delayable(&fsm->servo_hold_work, servo_hold_timeout);
    k_work_init_delayable(&fsm->button_window_work, button_window_timeout);
    k_work_init_delayable(&fsm->emergency_work, emergency_hold_timeout);
    k_work_init_delayable(&fsm->emergency_led_work, emergency_led_blink_handler);

    if (!sensor_init(fsm->sensor, on_sensor_change)) {
        LOG_ERR("Fallo crítico al establecer el enlace con el sensor inductivo.");
        return false;
    }

    /* Creación dinámica y arranque del hilo de ejecución del motor */
    k_thread_create(&fsm->motor_thread_data, motor_stack, K_THREAD_STACK_SIZEOF(motor_stack),
                    motor_manager_thread, (void *)fsm, NULL, NULL,
                    K_PRIO_COOP(5), 0, K_NO_WAIT);

    LOG_INF("Módulo de la máquina de estados inicializado correctamente.");
    return true;
}

/* --- Despachador Centralizado de Eventos --- */

void app_fsm_dispatch(app_fsm_t *fsm, system_event_t event)
{
    /* Compuerta de exclusión mutua de seguridad para el estado apagado */
    if (fsm->current_state == STATE_SYSTEM_OFF) {
        servo_set_angle(fsm->servo, SERVO_BASE_ANGLE);
        if (event == EVENT_METAL_DETECTED || event == EVENT_METAL_CLEARED || event == EVENT_TIMEOUT) {
            return; 
        }
    }

    /* Intercepción prioritaria global para el evento de parada de emergencia */
    if (event == EVENT_EMERGENCY) {
        LOG_ERR("[ALERTA INDUSTRIAL] Parada de emergencia solicitada.");
        fsm->motor_speed = 0;
        l298h_stop(fsm->motor);
        servo_set_angle(fsm->servo, 0);
        k_sem_reset(&fsm->sem_motor_update);
        k_work_cancel_delayable(&fsm->servo_hold_work);
        k_work_reschedule(&fsm->emergency_led_work, K_MSEC(EMERGENCY_BLINK_MS));
        fsm->current_state = STATE_EMERGENCY_STOP;
        return;
    }

    /* Redireccionamiento inmediato utilizando la tabla indexada O(1) */
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
        LOG_WRN("[DETECCIÓN] Elemento metálico localizado en la línea.");
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
    else if (event == EVENT_CLICK_DECREMENT) {
        if (fsm->motor_speed >= MOTOR_MIN_SPEED_PERCENT + MOTOR_SPEED_STEP) {
            fsm->motor_speed -= MOTOR_SPEED_STEP;
        } else {
            fsm->motor_speed = MOTOR_MIN_SPEED_PERCENT;
            servo_set_angle(fsm->servo, SERVO_BASE_ANGLE);
            k_work_cancel_delayable(&fsm->servo_hold_work); 
            fsm->current_state = STATE_SYSTEM_OFF;
        }
        k_sem_give(&fsm->sem_motor_update);
    }
}

static void handle_state_emergency_stop(app_fsm_t *fsm, system_event_t event)
{
    if (event == EVENT_BUTTON_PRESSED) {
        LOG_INF("Reinicio del sistema posterior a parada de emergencia aceptado.");
        k_work_cancel_delayable(&fsm->emergency_led_work); 
        k_work_cancel_delayable(&fsm->button_window_work); 
        atomic_set(&fsm->click_count, 0); 
           
        gpio_pin_set_dt(fsm->user_led, 0); 
        fsm->motor_speed = MOTOR_MIN_SPEED_PERCENT;
        servo_set_angle(fsm->servo, SERVO_BASE_ANGLE);
        fsm->current_state = STATE_SYSTEM_OFF;
    }
}