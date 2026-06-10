/**
 * @file button_api.c
 * @brief Implementación del filtrado temporal y decodificación de clics aislada de la FSM.
 */

#include "button_api.h"
#include "app_fsm.h"  // Necesario exclusivamente para obtener los ID de los enums de eventos
#include <zephyr/kernel.h>

/* --- Callbacks Internos de Tareas Diferidas (Work Queues) --- */

static void button_window_timeout(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    button_handler_t *btn = CONTAINER_OF(dwork, button_handler_t, button_window_work);

    if (btn->user_led) {
        gpio_pin_set_dt(btn->user_led, 0);
    }
   
    /* Si la ventana expira y solo hubo un clic, notificamos incremento de velocidad */
    if (atomic_get(&btn->click_count) == 1) {
        if (btn->callback) {
            btn->callback(btn->context, EVENT_CLICK_INCREMENT);
        }
    }
    atomic_set(&btn->click_count, 0);
}

static void emergency_hold_timeout(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    button_handler_t *btn = CONTAINER_OF(dwork, button_handler_t, emergency_work);

    k_work_cancel_delayable(&btn->button_window_work);
    atomic_set(&btn->click_count, 0);

    /* Despacho inmediato del evento crítico de parada de emergencia */
    if (btn->callback) {
        btn->callback(btn->context, EVENT_EMERGENCY);
    }
}

/* --- Implementación de la API Pública --- */

bool button_api_init(button_handler_t *btn, const struct gpio_dt_spec *gpio, 
                     const struct gpio_dt_spec *led, button_callback_t callback, void *context)
{
    if (!btn || !gpio || !callback) {
        return false;
    }

    btn->gpio = gpio;
    btn->user_led = led;
    btn->callback = callback;
    btn->context = context;
    btn->last_valid_time = 0;
    
    atomic_set(&btn->click_count, 0);
    atomic_set(&btn->immediate_mode, 0);

    /* Inicialización de las tareas diferidas vinculadas al contexto del objeto */
    k_work_init_delayable(&btn->button_window_work, button_window_timeout);
    k_work_init_delayable(&btn->emergency_work, emergency_hold_timeout);

    return true;
}

void button_api_notify_state(button_handler_t *btn, bool is_pressed)
{
    uint32_t current_time = k_uptime_get_32();

    /* Bloqueo temporal por software (Debounce) */
    if ((current_time - btn->last_valid_time) < BUTTON_DEBOUNCE_MS) {
        return;
    }
    btn->last_valid_time = current_time;

    if (is_pressed) {
        /* CRÍTICO EN TIEMPO REAL: Si está en modo inmediato, salta las ventanas de tiempo */
        if (atomic_get(&btn->immediate_mode) != 0) {
            if (btn->callback) {
                btn->callback(btn->context, EVENT_BUTTON_PRESSED);
            }
            return;
        }

        /* Comportamiento Estándar de Operación */
        if (btn->callback) {
            btn->callback(btn->context, EVENT_BUTTON_PRESSED);
        }
        
        k_work_reschedule(&btn->emergency_work, K_MSEC(BUTTON_EMERGENCY_HOLD_MS));
        atomic_inc(&btn->click_count);
       
        if (atomic_get(&btn->click_count) == 1) {
            if (btn->user_led) {
                gpio_pin_set_dt(btn->user_led, 1);
            }
            k_work_reschedule(&btn->button_window_work, K_MSEC(BUTTON_WINDOW_MS));
        }
        else if (atomic_get(&btn->click_count) == 2) {
            k_work_cancel_delayable(&btn->button_window_work);
            k_work_cancel_delayable(&btn->emergency_work);
           
            if (btn->user_led) {
                gpio_pin_set_dt(btn->user_led, 0);
            }
            
            if (btn->callback) {
                btn->callback(btn->context, EVENT_CLICK_DECREMENT);
            }
            atomic_set(&btn->click_count, 0);
        }
    } else {
        /* Al soltar el botón, cancelamos el temporizador de presión prolongada */
        if (atomic_get(&btn->immediate_mode) == 0) {
            k_work_cancel_delayable(&btn->emergency_work);
        }
    }
}

void button_api_set_immediate_mode(button_handler_t *btn, bool enabled)
{
    if (btn) {
        atomic_set(&btn->immediate_mode, enabled ? 1 : 0);
        if (enabled) {
            /* Purgar de forma segura tareas remanentes al entrar en emergencia */
            k_work_cancel_delayable(&btn->button_window_work);
            k_work_cancel_delayable(&btn->emergency_work);
            atomic_set(&btn->click_count, 0);
        }
    }
}