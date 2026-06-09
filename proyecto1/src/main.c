/**
 * @file main.c
 * @brief Inicializador y orquestador del sistema de la banda transportadora.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "main.h"

/* Registro del canal de diagnóstico exclusivo para el hilo principal de arranque */
LOG_MODULE_REGISTER(main_app, LOG_LEVEL_INF);

/* --- Declaración de Instancias de Hardware vinculadas al Devicetree --- */
static const struct gpio_dt_spec user_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec user_led    = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const SG90_Servo_t my_servo = {
    .pwm_spec = { .dev = DEVICE_DT_GET(DT_NODELABEL(pwm3)), .channel = 1, .period = PWM_MSEC(20), .flags = PWM_POLARITY_NORMAL }
};

static const L298H_Motor_t my_motor = {
    .in1 = { .port = DEVICE_DT_GET(DT_NODELABEL(gpiob)), .pin = 0, .dt_flags = GPIO_ACTIVE_HIGH },
    .in2 = { .port = DEVICE_DT_GET(DT_NODELABEL(gpiob)), .pin = 3, .dt_flags = GPIO_ACTIVE_HIGH },
    .pwm = { .dev = DEVICE_DT_GET(DT_NODELABEL(pwm2)), .channel = 1, .period = PWM_USEC(100), .flags = PWM_POLARITY_NORMAL }
};

static const LJ12A3_Sensor_t metal_sensor = {
    .gpio_spec = { .port = DEVICE_DT_GET(DT_NODELABEL(gpioa)), .pin = 1, .dt_flags = GPIO_ACTIVE_HIGH | GPIO_PULL_UP }
};

/* Instancia de contexto aislada y protegida para el control de esta banda transportadora */
static app_fsm_t banda_transportadora_fsm;

/* Estructura para registrar el enlace de la interrupción física del botón */
static struct gpio_callback button_cb_data;

/* Estructura para la tarea asíncrona del antirebote (Debounce) */
static struct k_work_delayable button_debounce_work;

/**
 * @brief Manejador de trabajo diferido para el antirebote del botón.
 */
static void button_debounce_handler(struct k_work *work)
{
    /* Evitamos advertencias del compilador indicando que el argumento 'work' no se procesará directamente */
    ARG_UNUSED(work);
    
    /* Lectura del estado eléctrico estable del pin (1 = Presionado, 0 = Liberado) */
    bool is_pressed = (gpio_pin_get_dt(&user_button) == 1); 
    
    /* Envío del estado limpio a la instancia específica de nuestra máquina de estados */
    app_fsm_notify_button_state(&banda_transportadora_fsm, is_pressed);                
}

/**
 * @brief Rutina de Servicio de Interrupción (ISR) del Botón de Usuario.
 */
static void button_pressed_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins);
    
    /* Reprogramación asíncrona del temporizador para evadir el ruido metálico */
    k_work_reschedule(&button_debounce_work, K_MSEC(30));                
}

/**
 * @brief Punto de entrada principal de la aplicación.
 */
int main(void)
{
    LOG_INF("[SISTEMA] Iniciando firmware de la planta de clasificación...");

    /* Validación física de la disponibilidad de los controladores base del chip */
    if (!device_is_ready(user_led.port) || !device_is_ready(user_button.port)) {
        LOG_ERR("Error crítico: Los periféricos base del microcontrolador no están listos.");
        return -ENODEV;
    }
    
    /* Configuración inicial de los canales lógicos de entrada y salida */
    gpio_pin_configure_dt(&user_led, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&user_button, GPIO_INPUT);

    /* Inicialización del hilo de trabajo asignado al filtro antirebote */
    k_work_init_delayable(&button_debounce_work, button_debounce_handler);

    /* Ajuste de la interrupción para que responda ante cualquier cambio eléctrico */
    gpio_pin_interrupt_configure_dt(&user_button, GPIO_INT_EDGE_BOTH); 
    
    /* Vinculación del pin físico con la función de respuesta rápida de la interrupción */
    gpio_init_callback(&button_cb_data, button_pressed_callback, BIT(user_button.pin));
    
    /* Activación oficial del canal de interrupción dentro del Kernel del sistema operativo */
    gpio_add_callback(user_button.port, &button_cb_data);

    /* Inicialización de la máquina de estados pasando la dirección de nuestra instancia local */
    if (!app_fsm_init(&banda_transportadora_fsm, &my_motor, &my_servo, &metal_sensor, &user_led)) {
        LOG_ERR("Error fatal: No se pudo arrancar la lógica de control FSM.");
        return -EIO;
    }

    LOG_INF("[SISTEMA] Planta en línea y operando de forma segura.");

    /* Suspensión perpetua y de bajo consumo para el hilo de arranque principal */
    while (1) {
        k_sleep(K_FOREVER);
    }

    return 0;
}