/**
 * @file sg90_api.h
 * @brief API de abstracción para el control del servomotor SG90 en Zephyr RTOS.
 * @note Totalmente portable, no bloqueante y adaptada a los rangos reales del SG90.
 */

// Guardas de seguridad para evitar que el archivo se procese dos veces al compilar
#ifndef SG90_API_H_
#define SG90_API_H_

// Incluye las herramientas de Zephyr para manejar las señales de pulso PWM que mueven los servos
#include <zephyr/drivers/pwm.h>
// Tipo de dato booleano (true / false)
#include <stdbool.h>
// Tipos de datos enteros estándar (uint32_t, etc.)
#include <stdint.h>

// Permite la compatibilidad del código si se llega a enlazar con un proyecto en C++
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Estructura de control del servomotor.
 * Almacena la especificación de hardware del Devicetree de Zephyr.
 */
// Caja que guarda la información del pin físico que genera la señal para el servo
typedef struct {
    const struct pwm_dt_spec pwm_spec; // Datos del Timer, canal y periodo asignados al servo
} SG90_Servo_t;

/**
 * @brief Inicializa el servomotor y verifica que el driver de Zephyr esté listo.
 * @param servo Puntero a la estructura del servomotor.
 * @return true si la inicialización fue exitosa, false en caso contrario.
 */
// Comprueba que el periférico de hardware esté encendido y listo para recibir órdenes
bool servo_init(const SG90_Servo_t *servo);

/**
 * @brief Mueve el brazo del servomotor a un ángulo específico (no bloqueante).
 * @param servo Puntero a la estructura del servomotor.
 * @param angle Ángulo deseado (0 a 180 grados).
 * @return 0 si fue exitoso, código de error negativo (errno) si falló.
 */
// Calcula el pulso necesario y cambia la posición del brazo del servo de inmediato
int servo_set_angle(const SG90_Servo_t *servo, uint32_t angle);

#ifdef __cplusplus
}
#endif

#endif /* SG90_API_H_ */