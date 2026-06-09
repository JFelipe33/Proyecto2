/**
 * @file l298h_api.h
 * @brief API de control para puente H L298N (Zephyr RTOS, sin delays bloqueantes)
 * @note Completamente portable, thread-safe y basada en especificaciones Devicetree.
 */

// Guardas de seguridad para impedir que este archivo se procese dos veces al compilar
#ifndef L298H_API_H_
#define L298H_API_H_

// Incluye las herramientas básicas de Zephyr para manejar pines digitales (GPIO)
#include <zephyr/drivers/gpio.h>
// Incluye las herramientas de Zephyr para manejar señales de velocidad (PWM)
#include <zephyr/drivers/pwm.h>
// Tipos de datos estándar como enteros de 8, 32 bits, etc.
#include <stdint.h>
// Tipo de dato booleano (true / false)
#include <stdbool.h>

// Permite que este código en C sea compatible si se llega a usar en un proyecto de C++
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Estructura que describe un motor controlado por L298.
 * - `in1` / `in2`: Pines GPIO que definen la dirección del puente H.
 * - `pwm`: Especificación del canal PWM conectado al pin EN (Enable).
 */
// Caja de herramientas que agrupa los pines físicos que usa el motor
typedef struct {
    const struct gpio_dt_spec in1; // Pin físico de dirección 1 (IN1)
    const struct gpio_dt_spec in2; // Pin físico de dirección 2 (IN2)
    const struct pwm_dt_spec pwm;  // Pin físico de velocidad (ENA / ENB) usando PWM
} L298H_Motor_t;

/**
 * @brief Dirección y modos de operación física del motor.
 */
// Opciones disponibles para decirle al motor hacia dónde o cómo moverse
typedef enum {
    L298H_DIR_FORWARD = 0, /**< Giro hacia adelante */ // El motor gira hacia el frente
    L298H_DIR_REVERSE,     /**< Giro inverso */     // El motor gira hacia atrás
    L298H_DIR_COAST,       /**< Parada suave por inercia (Pines en LOW) */ // Se apaga y se frena solo por la fricción
    L298H_DIR_BRAKE        /**< Freno electrónico dinámico (Pines en HIGH) */ // Freno seco forzando eléctricamente al motor
} L298H_Direction_t;

/**
 * @brief Inicializa el motor: configura GPIOs y limpia el estado del PWM.
 * @param motor Puntero a la estructura de especificación del motor.
 * @return true si la inicialización y validación del hardware fue exitosa.
 */
// Prepara los pines del motor como salidas y los deja listos para operar
bool l298h_init(const L298H_Motor_t *motor);

/**
 * @brief Establece la dirección del motor de manera inmediata (no bloqueante).
 * @param motor Puntero a la estructura del motor.
 * @param dir Dirección o modo deseado.
 * @return 0 si la operación fue exitosa, <0 (código de error errno) en caso de falla.
 */
// Cambia los pines IN1 e IN2 para decidir el sentido de giro
int l298h_set_direction(const L298H_Motor_t *motor, L298H_Direction_t dir);

/**
 * @brief Establece la velocidad del motor basándose en un porcentaje (0 a 100%).
 * @param motor Puntero a la estructura del motor.
 * @param speed_percent Velocidad en porcentaje válido (0 = parado, 100 = máximo).
 * @return 0 si la operación fue exitosa, <0 en caso de falla.
 */
// Calcula y aplica el pulso PWM para acelerar o desacelerar el motor
int l298h_set_speed(const L298H_Motor_t *motor, uint8_t speed_percent);

/**
 * @brief Frena dinámicamente el motor aplicando torque inverso interno.
 * @param motor Puntero a la estructura del motor.
 * @return 0 si la operación fue exitosa, <0 en caso de falla.
 */
// Pone el PWM al máximo y ambos pines IN en alto para clavar el motor al instante
int l298h_brake(const L298H_Motor_t *motor);

/**
 * @brief Detiene la marcha del motor de forma libre (inercia). Pulso PWM a 0.
 * @param motor Puntero a la estructura del motor.
 * @return 0 si la operación fue exitosa, <0 en caso de falla.
 */
// Apaga la señal de velocidad y de dirección para que ruede libre hasta detenerse
int l298h_stop(const L298H_Motor_t *motor);

#ifdef __cplusplus
}
#endif

#endif /* L298H_API_H_ */