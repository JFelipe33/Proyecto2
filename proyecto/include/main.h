/**
 * @file main.h
 * @brief Configuración global del firmware y asignación de periféricos.
 */

// Guardas de seguridad para evitar que este archivo se compile más de una vez
#ifndef MAIN_H_
#define MAIN_H_

/**
 * =============================================================================
 * ASIGNACIÓN DE HARDWARE (Mapeo físico Nucleo-L476RG)
 * =============================================================================
 * * CONTROL DEL MOTOR (Puente H L298N):
 * - PA0 (TIM2 CH1 / alias pwm2)   -> Entrada ENA (Velocidad PWM).
 * - PB0 (GPIO / Activo Alto)      -> Entrada IN1 (Dirección).
 * - PB3 (GPIO / Activo Alto)      -> Entrada IN2 (Dirección).
 * * CLASIFICACIÓN (Servo SG90):
 * - PB4 (TIM3 CH1 / alias pwm3)   -> Entrada de señal PWM (Ángulo).
 * * SENSORES Y ENTRADAS DIGITALES:
 * - PA1 (GPIO / Pull-Up)          -> Entrada del Sensor Inductivo LJ12A3.
 * - PC13 (GPIO / alias sw0)       -> Botón de usuario (Control de clics).
 * * SALIDAS DE ESTADO:
 * - PA5 (GPIO / alias led0)       -> LED de usuario (Feedback visual).
 * =============================================================================
 */

// Incluye la API del Puente H para controlar la velocidad y sentido del motor
#include "l298h_api.h"
// Incluye la API del Servomotor para mover el brazo clasificador
#include "sg90_api.h"
// Incluye la API del Sensor Inductivo para detectar los objetos metálicos
#include "lj12a3_api.h"
// Incluye la API de la Máquina de Estados que gobierna la lógica de la banda
#include "app_fsm.h"

/**
 * @brief Instancia global de la Máquina de Estados de la aplicación.
 */
// Declaración externa para que cualquier archivo que use main.h sepa que existe 'app_fsm'
extern app_fsm_t app_fsm;

#endif /* MAIN_H_ */