/**
 * @file sg90_api.c
 * @brief Implementation limpia y optimizada de las funciones de control para el SG90.
 */

// Incluye las firmas de las funciones del servo que definimos en el archivo .h
#include "sg90_api.h"
// Incluye las funciones del núcleo de Zephyr RTOS
#include <zephyr/kernel.h>
// Incluye los códigos de error estándar del sistema operativo
#include <errno.h>

/* Rangos reales del pulso PWM para un servomotor SG90 comercial (0 a 180 grados) */
#define SG90_MIN_PULSE_USEC 510U   /**< Pulso para 0 grados (510 microsegundos) */ // Duración mínima del pulso en alto para la posición inicial
#define SG90_MAX_PULSE_USEC 2500U  /**< Pulso para 180 grados (2500 microsegundos) */ // Duración máxima del pulso en alto para la posición final
#define SG90_MAX_ANGLE      180U   // El límite físico de giro en grados del servo común

// Revisa que el hardware del servo esté listo al arrancar
bool servo_init(const SG90_Servo_t *servo)
{
    // Validación de seguridad: si el puntero está vacío, aborta devolviendo falso
    if (servo == NULL) {
        return false;
    }

    /* Verifica si el controlador de hardware asignado en el devicetree está listo */
    // Pregunta a Zephyr si el Timer asignado al canal PWM del servo ya arrancó bien
    return device_is_ready(servo->pwm_spec.dev);
}

// Recibe un ángulo en grados y lo transforma en la señal PWM real que entiende el servo
int servo_set_angle(const SG90_Servo_t *servo, uint32_t angle)
{
    /* 1. Validaciones de seguridad de software y hardware */
    // Si no nos pasan un servo válido, devuelve error de argumento inválido (-EINVAL)
    if (servo == NULL) {
        return -EINVAL;
    }

    // Si el periférico PWM no está disponible en la placa, devuelve error de dispositivo ausente (-ENODEV)
    if (!device_is_ready(servo->pwm_spec.dev)) {
        return -ENODEV;
    }

    /* 2. Restricción física de seguridad para el SG90 */
    // Si piden un ángulo mayor a 180 grados, el limitador lo clava en 180 para no forzar los engranajes
    if (angle > SG90_MAX_ANGLE) {
        angle = SG90_MAX_ANGLE;
    }

    /* 3. Conversión de límites de tiempo a nanosegundos */
    // Convierte los microsegundos definidos arriba (510 y 2500) a nanosegundos, que es la unidad que usa Zephyr
    uint32_t min_ns = PWM_USEC(SG90_MIN_PULSE_USEC);
    uint32_t max_ns = PWM_USEC(SG90_MAX_PULSE_USEC);

    /* 4. Ecuación de interpolación lineal basada en enteros (Evita punto flotante) */
    // Aplica la fórmula matemática para calcular exactamente cuántos nanosegundos debe durar el pulso alto en base al ángulo
    uint32_t pulse_ns = min_ns + (((max_ns - min_ns) * angle) / SG90_MAX_ANGLE);

    /* 5. Actualizar el ciclo de trabajo del PWM de forma inmediata */
    // Le inyecta el pulso calculado al driver de Zephyr para mover físicamente el brazo del servo
    return pwm_set_pulse_dt(&servo->pwm_spec, pulse_ns);
}