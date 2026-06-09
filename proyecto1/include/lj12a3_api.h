/**
 * @file lj12a3_api.h
 * @brief API de abstracción para el sensor inductivo de metales LJ12A3 en Zephyr RTOS.
 * @note Sensor NPN con salida a colector abierto (Open-Collector). 
 * Inicialmente al aire (flotando) y se cierra mandando la salida a Tierra (0V) al detectar metal.
 * Genera interrupciones sin delays bloqueantes.
 */

// Guardas de seguridad para evitar que el compilador procese dos veces este archivo
#ifndef LJ12A3_API_H_
#define LJ12A3_API_H_

// Incluye las herramientas de Zephyr para leer y configurar pines digitales (GPIO)
#include <zephyr/drivers/gpio.h>
// Tipo de dato booleano (true / false)
#include <stdbool.h>

/**
 * @brief Estructura de control del sensor inductivo.
 * Almacena la especificación de GPIO del Devicetree de Zephyr.
 */
// Caja que guarda la información del pin físico donde está conectado el sensor
typedef struct {
    const struct gpio_dt_spec gpio_spec; // Información del puerto y número de pin del microcontrolador
} LJ12A3_Sensor_t;

/**
 * @brief Callback ejecutada cuando el sensor detecta un metal.
 * @param sensor Puntero a la estructura del sensor.
 */
// Crea un "alias" o molde para una función que se ejecutará automáticamente cuando el sensor cambie
typedef void (*sensor_callback_t)(const LJ12A3_Sensor_t *sensor);

/**
 * @brief Inicializa el sensor inductivo y configura su interrupción.
 * @param sensor Puntero a la estructura del sensor.
 * @param callback Función a ejecutar cuando se dispare la interrupción.
 * @return true si la inicialización fue exitosa, false en caso contrario.
 */
// Configura el pin del sensor como entrada y prepara las interrupciones eléctricas
bool sensor_init(const LJ12A3_Sensor_t *sensor, sensor_callback_t callback);

/**
 * @brief Lee el estado actual del sensor (instantáneo, sin bloqueo).
 * @param sensor Puntero a la estructura del sensor.
 * @return true si se detecta metal (salida HIGH), false si no hay metal (salida LOW).
 */
// Revisa de forma rápida y en tiempo real si hay metal frente al sensor en este instante
bool sensor_metal_detected(const LJ12A3_Sensor_t *sensor);

#endif /* LJ12A3_API_H_ */