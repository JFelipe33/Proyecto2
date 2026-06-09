/**
 * @file main.c
 * @brief Inicializador y orquestador del sistema de la banda transportadora.
 */

// Incluye las funciones del sistema operativo Zephyr (hilos, tiempos de espera)
#include <zephyr/kernel.h>
// Incluye las herramientas de control de dispositivos y controladores de Zephyr
#include <zephyr/device.h>
// Incluye las herramientas para controlar pines digitales de entrada y salida (GPIO)
#include <zephyr/drivers/gpio.h>
// Incluye las definiciones globales de hardware de nuestro proyecto
#include "main.h"
// Incluye la estructura y funciones de control de la máquina de estados
#include "app_fsm.h"

/* Periféricos integrados de la placa Nucleo */
static const struct gpio_dt_spec user_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec user_led    = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* Instancias de hardware externo */
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

// Estructura interna necesaria para guardar el enlace de la interrupción del botón
static struct gpio_callback button_cb_data;

/* --- ARQUITECTURA PROFESIONAL: Estructuras para filtrado de flanco de cola --- */
static struct k_work_delayable button_debounce_work;

/**
 * @brief Manejador de trabajo diferido para el antirebote.
 * Se ejecuta de manera asíncrona fuera del contexto de interrupción (ISR),
 * exactamente 30ms después del ÚLTIMO rebote físico detectado en la línea.
 */
static void button_debounce_handler(struct k_work *work)
{
    // Leemos el estado eléctrico de la línea cuando ya se encuentra 100% estable.
    // (1 = presionado firmemente, 0 = liberado por completo)
    bool is_pressed = (gpio_pin_get_dt(&user_button) == 1); 
    
    // Enviamos un evento puro, garantizado y libre de ruido a la FSM
    app_fsm_notify_button_state(is_pressed);                
}

/* ISR del Botón de Usuario */
static void button_pressed_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins);
    
    /* * PRINCIPIO ARQUITECTÓNICO: No muestrear el pin aquí adentro.
     * Cada flanco (tanto bajada como subida del rebote) pospone la ejecución 30ms adicionales.
     * El temporizador solo expirará cuando cese completamente la conmutación parásita.
     */
    k_work_reschedule(&button_debounce_work, K_MSEC(30));                
}

// Punto de entrada principal donde arranca el firmware al encender la placa
int main(void)
{
    /* 1. Inicializar Hardware Base (LED y Botón) */
    if (!device_is_ready(user_led.port) || !device_is_ready(user_button.port)) {
        return -ENODEV;
    }
    
    // Configura el LED de la placa como una salida digital y se asegura de que inicie apagado
    gpio_pin_configure_dt(&user_led, GPIO_OUTPUT_INACTIVE);
    // Configura el botón de la placa como una entrada digital para leer las pulsaciones
    gpio_pin_configure_dt(&user_button, GPIO_INPUT);

    // Inicializamos el trabajo diferido del antirebote antes de habilitar la interrupción
    k_work_init_delayable(&button_debounce_work, button_debounce_handler);

    // Configura la interrupción para que reaccione a ambos flancos (presión y liberación)
    gpio_pin_interrupt_configure_dt(&user_button, GPIO_INT_EDGE_BOTH); 
    // Enlaza el pin del botón con nuestra función de respuesta rápida 'button_pressed_callback'
    gpio_init_callback(&button_cb_data, button_pressed_callback, BIT(user_button.pin));
    // Activa oficialmente la interrupción en el sistema operativo
    gpio_add_callback(user_button.port, &button_cb_data);

    /* 2. Inicializar la FSM de la aplicación (Levanta drivers, hilos y semáforos) */
    if (!app_fsm_init(&app_fsm, &my_motor, &my_servo, &metal_sensor, &user_led)) {
        return -EIO;
    }

    /* 3. El hilo principal duerme permanentemente de forma eficiente */
    while (1) {
        k_sleep(K_FOREVER);
    }

    return 0;
}