/**
 * @file l298h_api.c
 * @brief Implementación limpia y optimizada de la API para el puente H L298N.
 */

// Incluye las firmas de las funciones que vamos a programar aquí
#include "l298h_api.h"
// Incluye las funciones del núcleo de Zephyr RTOS
#include <zephyr/kernel.h>
// Incluye los códigos de error estándar del sistema (como -EINVAL o -ENODEV)
#include <errno.h>

/**
 * @brief Recupera de forma segura el periodo PWM del devicetree en nanosegundos.
 * Si no está definido en el overlay, recurre a un valor seguro por defecto (10 kHz).
 */
// Función interna rápida para averiguar el "ancho total de ciclo" configurado para el PWM
static inline uint32_t l298h_get_period_ns(const L298H_Motor_t *m)
{
    // Si el archivo de configuración (.overlay) tiene un periodo válido, usa ese
    if (m->pwm.period != 0U) {
        return m->pwm.period;
    }
    /* Fallback a 10 kHz (100,000 ns) si no se especifica en el Devicetree */
    // Si no se configuró nada, usa 100,000 nanosegundos como un salvavidas seguro
    return 100000U; 
}

/**
 * @brief Valida de forma interna si los controladores de hardware están listos.
 */
// Función interna para revisar si los chips o periféricos físicos ya encendieron bien
static inline bool l298h_is_hardware_ready(const L298H_Motor_t *m)
{
    // Verifica que el puerto de IN1, el puerto de IN2 y el controlador del PWM estén listos
    return (device_is_ready(m->in1.port) && 
            device_is_ready(m->in2.port) && 
            device_is_ready(m->pwm.dev));
}

// Configura los pines del motor al iniciar el microcontrolador
bool l298h_init(const L298H_Motor_t *motor)
{
    int ret; // Variable para guardar el resultado de las configuraciones y ver si fallaron

    // Si el puntero al motor está vacío, aborta para no causar fallos de memoria
    if (motor == NULL) {
        return false;
    }

    // Si el hardware no responde o no está listo en el sistema, aborta
    if (!l298h_is_hardware_ready(motor)) {
        return false;
    }

    /* Configurar los pines IN de control como salidas digitales */
    // Configura el pin IN1 como salida digital e inicia apagado (0 lógico)
    ret = gpio_pin_configure_dt(&motor->in1, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        return false; // Si dio error la configuración del pin, sale retornando falso
    }
    
    // Configura el pin IN2 como salida digital e inicia apagado (0 lógico)
    ret = gpio_pin_configure_dt(&motor->in2, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        return false; // Si dio error, sale retornando falso
    }

    /* Asegurar que el canal PWM inicie apagado (0 nsec de pulso) */
    // Configura el pulso de velocidad en 0 para que el motor empiece totalmente quieto
    ret = pwm_set_pulse_dt(&motor->pwm, 0U);
    if (ret != 0) {
        return false; // Si el PWM falló al configurarse, sale retornando falso
    }

    return true; // Si llegó hasta acá sin problemas, todo salió perfecto
}

// Cambia la combinación de los pines IN1 e IN2 según el sentido de giro deseado
int l298h_set_direction(const L298H_Motor_t *motor, L298H_Direction_t dir)
{
    // Si no pasaron un motor válido, devuelve error de argumento inválido (-EINVAL)
    if (motor == NULL) {
        return -EINVAL;
    }

    // Si el hardware no está disponible, devuelve error de dispositivo ausente (-ENODEV)
    if (!l298h_is_hardware_ready(motor)) {
        return -ENODEV;
    }

    // Evalúa cuál dirección se solicitó
    switch (dir) {
    case L298H_DIR_FORWARD: // Si es hacia adelante:
        gpio_pin_set_dt(&motor->in1, 1); // Pone IN1 en 1 (Alto)
        gpio_pin_set_dt(&motor->in2, 0); // Pone IN2 en 0 (Bajo)
        break;
    case L298H_DIR_REVERSE: // Si es reversa:
        gpio_pin_set_dt(&motor->in1, 0); // Pone IN1 en 0 (Bajo)
        gpio_pin_set_dt(&motor->in2, 1); // Pone IN2 en 1 (Alto)
        break;
    case L298H_DIR_COAST: // Si es parada libre por inercia:
        gpio_pin_set_dt(&motor->in1, 0); // Desenergiza IN1 (0)
        gpio_pin_set_dt(&motor->in2, 0); // Desenergiza IN2 (0)
        break;
    case L298H_DIR_BRAKE: // Si es freno de golpe eléctrico:
        gpio_pin_set_dt(&motor->in1, 1); // Pone IN1 en 1 (Alto)
        gpio_pin_set_dt(&motor->in2, 1); // Pone IN2 en 1 (Alto) para bloquear el motor
        break;
    default: // Si pasan una opción rara o inexistente:
        return -EINVAL; // Devuelve error de argumento inválido
    }

    return 0; // Terminó con éxito
}

// Modifica la velocidad del motor calculando el ciclo de trabajo del PWM
int l298h_set_speed(const L298H_Motor_t *motor, uint8_t speed_percent)
{
    // Validación: que el motor exista
    if (motor == NULL) {
        return -EINVAL;
    }

    // Validación: que los periféricos respondan
    if (!l298h_is_hardware_ready(motor)) {
        return -ENODEV;
    }

    // Limitador de seguridad: si piden más del 100%, lo baja a 100% de inmediato
    if (speed_percent > 100U) {
        speed_percent = 100U;
    }

    // Obtiene el tiempo total del periodo en nanosegundos
    uint32_t period_ns = l298h_get_period_ns(motor);
    // Regla de tres matemática: calcula cuántos nanosegundos debe durar el pulso arriba según el % pedido
    uint32_t pulse_ns = (uint32_t)(((uint64_t)period_ns * speed_percent) / 100U);

    // Le inyecta el pulso calculado al driver de PWM de Zephyr para cambiar la velocidad real
    return pwm_set_pulse_dt(&motor->pwm, pulse_ns);
}

// Ejecuta la maniobra de parada en seco forzando al puente H
int l298h_brake(const L298H_Motor_t *motor)
{
    // Validación estándar de seguridad
    if (motor == NULL) {
        return -EINVAL;
    }

    // Validación de hardware disponible
    if (!l298h_is_hardware_ready(motor)) {
        return -ENODEV;
    }

    /* CRÍTICO: Para frenado dinámico real, EN debe estar arriba (Habilitado) */
    // Obtiene el periodo completo en nanosegundos
    uint32_t period_ns = l298h_get_period_ns(motor);
    // Aplica el pulso al 100% (periodo completo) en el pin Enable para dar la máxima fuerza de freno
    pwm_set_pulse_dt(&motor->pwm, period_ns);

    // Al poner ambos canales IN del puente H en 1 lógico, las bobinas del motor entran en cortocircuito controlado, frenándolo en seco
    gpio_pin_set_dt(&motor->in1, 1);
    gpio_pin_set_dt(&motor->in2, 1);

    return 0; // Operación de frenado completada
}

// Apaga por completo la energía entregada al motor
int l298h_stop(const L298H_Motor_t *motor)
{
    // Validación estándar de seguridad
    if (motor == NULL) {
        return -EINVAL;
    }

    // Validación de hardware disponible
    if (!l298h_is_hardware_ready(motor)) {
        return -ENODEV;
    }

    /* Detener el motor dejando que se detenga por fricción (Desenergizado) */
    // Corta la señal de velocidad mandando el pulso a 0
    pwm_set_pulse_dt(&motor->pwm, 0U);
    // Apaga completamente ambos pines de dirección para quitarle toda la corriente eléctrica
    gpio_pin_set_dt(&motor->in1, 0);
    gpio_pin_set_dt(&motor->in2, 0);

    return 0; // Operación de parada completada con éxito
}