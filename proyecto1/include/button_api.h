/**
 * @file button_api.h
 * @brief API de control y decodificación asíncrona para botones en entornos industriales.
 * @note Totalmente orientado a objetos, thread-safe y desacoplado de la FSM de control.
 */

#ifndef BUTTON_API_H_
#define BUTTON_API_H_

#include <zephyr/types.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>

/* Tiempos de operación tomados de la especificación de la planta */
#define BUTTON_WINDOW_MS             1500U 
#define BUTTON_DEBOUNCE_MS           10U   
#define BUTTON_EMERGENCY_HOLD_MS     1500U 

/**
 * @brief Definición del Callback que usará el módulo para notificar eventos abstractos.
 */
typedef void (*button_callback_t)(void *context, uint32_t event_id);

/**
 * @brief Estructura de contexto reentrante para la gestión individualizada de botones.
 */
typedef struct button_handler {
    const struct gpio_dt_spec *gpio;          /**< Especificación física del pin GPIO */
    const struct gpio_dt_spec *user_led;      /**< Puntero de control hacia el LED indicador */
    button_callback_t callback;               /**< Función de notificación externa */
    void *context;                            /**< Puntero de contexto del cliente (ej. instancia FSM) */

    /* Estructuras de sincronización y tareas diferidas internas */
    struct k_work_delayable button_window_work; /**< Ventana de captura para clics múltiples */
    struct k_work_delayable emergency_work;     /**< Temporizador para validación de presión sostenida */
    
    /* Variables privadas de control protegidas para concurrencia */
    atomic_t click_count;                       /**< Contador atómico de pulsaciones válidas */
    uint32_t last_valid_time;                   /**< Estampa de tiempo para filtrado temporal debounce */
    atomic_t immediate_mode;                    /**< Flag de derivación rápida para parada de emergencia */
} button_handler_t;

/* --- API Pública de Control del Periférico --- */

/**
 * @brief Inicializa por completo el contexto del manejador del botón y sus tareas diferidas.
 */
bool button_api_init(button_handler_t *btn, const struct gpio_dt_spec *gpio, 
                     const struct gpio_dt_spec *led, button_callback_t callback, void *context);

/**
 * @brief Procesa el cambio eléctrico en el pin de entrada aplicando debounce y decodificación de eventos.
 */
void button_api_notify_state(button_handler_t *btn, bool is_pressed);

/**
 * @brief Modifica dinámicamente el comportamiento de filtrado del botón en caliente.
 * @param enabled true para activar despacho inmediato sin ventanas temporales de espera.
 */
void button_api_set_immediate_mode(button_handler_t *btn, bool enabled);

#endif /* BUTTON_API_H_ */