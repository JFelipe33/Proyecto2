/**
 * @file main.h
 * @brief Configuración global del firmware y asignación de periféricos.
 */

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

#include "l298h_api.h"
#include "sg90_api.h"
#include "lj12a3_api.h"
#include "app_fsm.h"

#endif /* MAIN_H_ */