/**
 * @file app_fsm.c
 * @brief Implementación del flujo de estados y control diferido.
 */

#include "app_fsm.h"
#include <zephyr/kernel.h>

app_fsm_t app_fsm;

/* Primitivas de sincronización y trabajos asíncronos diferidos */
static struct k_work sensor_work;                  
static struct k_work_delayable servo_hold_work;    
static struct k_work_delayable button_window_work;
static struct k_work_delayable emergency_work;    
static struct k_work_delayable emergency_led_work;

static volatile uint32_t click_count = 0;

/* Hilo cooperativo exclusivo para el Puente H */
K_THREAD_STACK_DEFINE(motor_stack, STACK_SIZE);
static struct k_thread motor_thread_data;
static struct k_sem sem_motor_update;    

/* --- Callbacks de Periféricos --- */

static void on_sensor_change(const LJ12A3_Sensor_t *sensor)
{
    k_work_submit(&sensor_work);
}

static void sensor_work_handler(struct k_work *work)
{
    if (sensor_metal_detected(app_fsm.sensor)) {
        app_fsm_dispatch(EVENT_METAL_DETECTED);
    } else {
        app_fsm_dispatch(EVENT_METAL_CLEARED);
    }
}

static void servo_hold_timeout(struct k_work *work)
{
    app_fsm_dispatch(EVENT_TIMEOUT);
}

static void button_window_timeout(struct k_work *work)
{
    gpio_pin_set_dt(app_fsm.user_led, 0);
   
    if (click_count == 1) {
        app_fsm_dispatch(EVENT_CLICK_INCREMENT);
    }
    click_count = 0;
}

static void emergency_hold_timeout(struct k_work *work)
{
    k_work_cancel_delayable(&button_window_work);
    click_count = 0;
    app_fsm_dispatch(EVENT_EMERGENCY);
}

static void emergency_led_blink_handler(struct k_work *work)
{
    static uint8_t led_state = 0;

    if (app_fsm.current_state == STATE_EMERGENCY_STOP) {
        led_state = !led_state;
        gpio_pin_set_dt(app_fsm.user_led, led_state);
        k_work_reschedule(&emergency_led_work, K_MSEC(EMERGENCY_BLINK_MS));
    }
}

/* --- Entrada Digital con Filtro Debounce --- */

void app_fsm_notify_button_state(bool is_pressed)
{
    static uint32_t last_valid_time = 0;
    uint32_t current_time = k_uptime_get_32();

    if ((current_time - last_valid_time) < BUTTON_DEBOUNCE_MS) {
        return;
    }
    last_valid_time = current_time;

    if (is_pressed) {
        app_fsm_dispatch(EVENT_BUTTON_PRESSED);

        k_work_reschedule(&emergency_work, K_MSEC(BUTTON_EMERGENCY_HOLD_MS));
        click_count++;
       
        if (click_count == 1) {
            if (app_fsm.current_state != STATE_EMERGENCY_STOP) {
                gpio_pin_set_dt(app_fsm.user_led, 1);
            }
            k_work_reschedule(&button_window_work, K_MSEC(BUTTON_WINDOW_MS));
        }
        else if (click_count == 2) {
            k_work_cancel_delayable(&button_window_work);
            k_work_cancel_delayable(&emergency_work);
           
            if (app_fsm.current_state != STATE_EMERGENCY_STOP) {
                gpio_pin_set_dt(app_fsm.user_led, 0);
            }
            app_fsm_dispatch(EVENT_CLICK_DECREMENT);
            click_count = 0;
        }
    } else {
        k_work_cancel_delayable(&emergency_work);
    }
}

/* --- Hilo Administrador del Motor --- */

static void motor_manager_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p2); ARG_UNUSED(p3);
    uint8_t last_speed = 0xFF;    
    app_fsm_t *fsm = (app_fsm_t *)p1;

    while (1) {
        k_sem_take(&sem_motor_update, K_FOREVER);

        if (fsm->motor_speed != last_speed) {
            if (fsm->motor_speed <= MOTOR_MIN_SPEED_PERCENT) {
                l298h_stop(fsm->motor);
            } else {
                l298h_set_direction(fsm->motor, L298H_DIR_FORWARD);
                l298h_set_speed(fsm->motor, fsm->motor_speed);
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

    fsm->motor = motor;
    fsm->servo = servo;
    fsm->sensor = sensor;
    fsm->user_led = led;
    fsm->current_state = STATE_SYSTEM_OFF;
    fsm->motor_speed = MOTOR_MIN_SPEED_PERCENT;

    if (!l298h_init(fsm->motor) || !servo_init(fsm->servo)) {
        return false;
    }

    // -------------------------------------------------------------------------
    // SOLUCIÓN INCONVENIENTE 1: Garantizar estado seguro inicial de hardware
    // -------------------------------------------------------------------------
    servo_set_angle(fsm->servo, 0); // Forzar síncronamente el servo a 0 grados
    l298h_stop(fsm->motor);        // Asegurar que el puente H arranque apagado

    k_sem_init(&sem_motor_update, 0, 1);
    k_work_init(&sensor_work, sensor_work_handler);
    k_work_init_delayable(&servo_hold_work, servo_hold_timeout);
    k_work_init_delayable(&button_window_work, button_window_timeout);
    k_work_init_delayable(&emergency_work, emergency_hold_timeout);
    k_work_init_delayable(&emergency_led_work, emergency_led_blink_handler);

    if (!sensor_init(fsm->sensor, on_sensor_change)) {
        return false;
    }

    k_thread_create(&motor_thread_data, motor_stack, K_THREAD_STACK_SIZEOF(motor_stack),
                    motor_manager_thread, (void *)fsm, NULL, NULL,
                    K_PRIO_COOP(5), 0, K_NO_WAIT);

    return true;
}

/* --- Despachador de Eventos FSM --- */

void app_fsm_dispatch(system_event_t event)
{
    // -------------------------------------------------------------------------
    // SOLUCIÓN INCONVENIENTE 2: Compuerta de Exclusión Mutua para STATE_SYSTEM_OFF
    // -------------------------------------------------------------------------
    if (app_fsm.current_state == STATE_SYSTEM_OFF) {
        // Obligamos físicamente al actuador a mantenerse en posición base
        servo_set_angle(app_fsm.servo, SERVO_BASE_ANGLE);
        
        // Si llega un evento del sensor o del temporizador rezagado, abortamos
        if (event == EVENT_METAL_DETECTED || event == EVENT_METAL_CLEARED || event == EVENT_TIMEOUT) {
            return; 
        }
    }

    system_state_t next_state = app_fsm.current_state;

    // Intercepción Global de Emergencia
    if (event == EVENT_EMERGENCY) {
        app_fsm.motor_speed = 0;
        l298h_stop(app_fsm.motor);
        servo_set_angle(app_fsm.servo, 0);
        k_sem_reset(&sem_motor_update);
        
        // Purgar la cola de hilos de trabajo delayables para evitar brincos
        k_work_cancel_delayable(&servo_hold_work);
       
        k_work_reschedule(&emergency_led_work, K_MSEC(EMERGENCY_BLINK_MS));
        app_fsm.current_state = STATE_EMERGENCY_STOP;
        return;
    }

    switch (app_fsm.current_state) {
       
    case STATE_SYSTEM_OFF:
        if (event == EVENT_CLICK_INCREMENT) {
            app_fsm.motor_speed = MOTOR_MIN_SPEED_PERCENT + MOTOR_SPEED_STEP;
            k_sem_give(&sem_motor_update);
            next_state = STATE_BANDA_RUNNING;
        }
        break;

    case STATE_BANDA_RUNNING:
        if (event == EVENT_CLICK_INCREMENT) {
            if (app_fsm.motor_speed + MOTOR_SPEED_STEP <= MOTOR_MAX_SPEED_PERCENT) {
                app_fsm.motor_speed += MOTOR_SPEED_STEP;
            } else {
                app_fsm.motor_speed = MOTOR_MAX_SPEED_PERCENT;
            }
            k_sem_give(&sem_motor_update);
        }
        else if (event == EVENT_CLICK_DECREMENT) {
            if (app_fsm.motor_speed >= MOTOR_MIN_SPEED_PERCENT + MOTOR_SPEED_STEP) {
                app_fsm.motor_speed -= MOTOR_SPEED_STEP;
            } else {
                app_fsm.motor_speed = MOTOR_MIN_SPEED_PERCENT;
            }
            k_sem_give(&sem_motor_update);

            if (app_fsm.motor_speed <= MOTOR_MIN_SPEED_PERCENT) {
                // Al transicionar a OFF, cancelamos por software el temporizador del servo
                k_work_cancel_delayable(&servo_hold_work);
                servo_set_angle(app_fsm.servo, 0);
                next_state = STATE_SYSTEM_OFF;
            }
        }
        else if (event == EVENT_METAL_DETECTED) {
            servo_set_angle(app_fsm.servo, SERVO_DETECTION_ANGLE);
            k_work_schedule(&servo_hold_work, K_MSEC(SERVO_DETECTION_HOLD_MS));
            next_state = STATE_METAL_REJECT;
        }
        break;

    case STATE_METAL_REJECT:
        if (event == EVENT_TIMEOUT) {
            if (sensor_metal_detected(app_fsm.sensor)) {
                k_work_schedule(&servo_hold_work, K_MSEC(SERVO_DETECTION_HOLD_MS));
            } else {
                servo_set_angle(app_fsm.servo, SERVO_BASE_ANGLE);
                next_state = STATE_BANDA_RUNNING;
            }
        }
        else if (event == EVENT_CLICK_DECREMENT) {
            if (app_fsm.motor_speed >= MOTOR_MIN_SPEED_PERCENT + MOTOR_SPEED_STEP) {
                app_fsm.motor_speed -= MOTOR_SPEED_STEP;
            } else {
                app_fsm.motor_speed = MOTOR_MIN_SPEED_PERCENT;
                servo_set_angle(app_fsm.servo, SERVO_BASE_ANGLE);
                k_work_cancel_delayable(&servo_hold_work); // Forzar la cancelación del timer activo
                next_state = STATE_SYSTEM_OFF;
            }
            k_sem_give(&sem_motor_update);
        }
        break;
       
    case STATE_EMERGENCY_STOP:
        if (event == EVENT_BUTTON_PRESSED) {
            k_work_cancel_delayable(&emergency_led_work); 
            k_work_cancel_delayable(&button_window_work); 
            click_count = 0; 
           
            gpio_pin_set_dt(app_fsm.user_led, 0); 
            app_fsm.motor_speed = MOTOR_MIN_SPEED_PERCENT;
            servo_set_angle(app_fsm.servo, SERVO_BASE_ANGLE);
            next_state = STATE_SYSTEM_OFF;
        }
        break;

    default:
        next_state = STATE_SYSTEM_OFF;
        break;
    }

    // Acción de resguardo final al confirmar el estado de apagado
    if (next_state == STATE_SYSTEM_OFF) {
        servo_set_angle(app_fsm.servo, SERVO_BASE_ANGLE);
    }

    app_fsm.current_state = next_state;
}