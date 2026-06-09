/**
 * @file lj12a3_api.c
 * @brief Implementación corregida de la API del sensor inductivo LJ12A3.
 */

// Incluye las firmas de las funciones del sensor que vamos a programar
#include "lj12a3_api.h"
// Incluye las funciones esenciales del núcleo de Zephyr RTOS
#include <zephyr/kernel.h>

/* Almacenar el callback del usuario para usarlo en la ISR */
// Variables globales internas para recordar qué función y qué sensor usar dentro de la interrupción
static sensor_callback_t user_callback = NULL;   // Guarda la función que el usuario quiere ejecutar
static const LJ12A3_Sensor_t *sensor_instance = NULL; // Guarda los datos del sensor actual

/* CRÍTICO: La estructura del callback DEBE ser global/estática a nivel de archivo */
// Estructura interna que exige Zephyr para registrar y enganchar la interrupción en el sistema
static struct gpio_callback sensor_cb;

// Esta es la función rápida de interrupción (ISR) que el microcontrolador ejecuta directo desde el hardware
static void sensor_isr_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins); // Le dice al compilador que ignore estos parámetros obligatorios de Zephyr

    // Si el usuario configuró una función de aviso y guardó el sensor correctamente
    if (user_callback != NULL && sensor_instance != NULL) {
        // Llama a la función del usuario pasándole los datos del sensor
        user_callback(sensor_instance);
    }
}

// Configura el pin del sensor y activa las alertas por interrupción
bool sensor_init(const LJ12A3_Sensor_t *sensor, sensor_callback_t callback)
{
    int ret; // Variable para almacenar códigos de error del sistema

    // Validación de seguridad: si no nos pasan un sensor o una función válida, aborta
    if (sensor == NULL || callback == NULL) {
        return false;
    }

    // Verifica si el puerto físico del chip (ej. GPIOA o GPIOB) ya está listo para usarse
    if (!device_is_ready(sensor->gpio_spec.port)) {
        return false;
    }

    // Guarda los punteros en las variables globales del archivo para usarlos luego en la interrupción
    sensor_instance = sensor;
    user_callback = callback;

    /* Configurar el pin usando las banderas del devicetree (añadiendo la dirección INPUT) */
    // Configura el pin físicamente para que actúe como una Entrada digital
    ret = gpio_pin_configure_dt(&sensor->gpio_spec, GPIO_INPUT);
    if (ret != 0) {
        return false; // Si la configuración falla, sale retornando falso
    }

    /* Inicializar el callback usando la variable estática global del archivo */
    // Registra nuestra función 'sensor_isr_callback' para que atienda al pin del sensor
    gpio_init_callback(&sensor_cb, sensor_isr_callback, BIT(sensor->gpio_spec.pin));

    // Añade y activa físicamente el callback en el puerto del microcontrolador
    ret = gpio_add_callback(sensor->gpio_spec.port, &sensor_cb);
    if (ret != 0) {
        return false; // Si el sistema no permite añadir la función, aborta
    }

    /* Escuchar cambios en ambos flancos */
    // Configura el pin para que salte la interrupción tanto cuando el metal entra como cuando sale (flanco de subida y bajada)
    ret = gpio_pin_interrupt_configure_dt(&sensor->gpio_spec, GPIO_INT_EDGE_BOTH);
    if (ret != 0) {
        return false; // Si falla la configuración de la interrupción, aborta
    }

    return true; // Todo se configuró con éxito
}

// Lee el voltaje del pin y deduce si hay metal según el comportamiento NPN Colector Abierto
bool sensor_metal_detected(const LJ12A3_Sensor_t *sensor)
{
    // Validación: si el sensor no es válido o el hardware no responde, asume que no hay metal
    if (sensor == NULL || !device_is_ready(sensor->gpio_spec.port)) {
        return false;
    }

    /* EXPLICACIÓN ELÉCTRICA: El sensor es NPN con salida a colector abierto. 
     * - Sin metal: El transistor interno está abierto, dejando la salida desconectada ("al aire"). 
     * El pin de la placa necesita una resistencia de Pull-Up para mantenerse en un "1" lógico estable.
     * - Con metal: El transistor interno se cierra y conecta la salida directamente a Tierra (0V).
     * Por lo tanto, si 'gpio_pin_get' lee un estado de 0 (bajo / Tierra), significa que ¡HAY METAL! */
    // Lee el estado eléctrico puro del pin: si detecta un 0 lógico (0V), confirma la detección de metal
    return gpio_pin_get(sensor->gpio_spec.port, sensor->gpio_spec.pin) == 0;
}