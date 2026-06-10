/**
 * @file l298h_api.c
 * @brief Implementación limpia y optimizada de la API para el puente H L298N con escalamiento lineal.
 */

#include "l298h_api.h"
#include <zephyr/kernel.h>
#include <errno.h>

/**
 * @brief Recupera de forma segura el periodo PWM del devicetree en nanosegundos.
 * Si no está definido en el overlay, recurre a un valor seguro por defecto (10 kHz).
 */
static inline uint32_t l298h_get_period_ns(const L298H_Motor_t *m)
{
    if (m->pwm.period != 0U) {
        return m->pwm.period;
    }
    /* Fallback a 10 kHz (100,000 ns) si no se especifica en el Devicetree */
    return 100000U; 
}

/**
 * @brief Valida de forma interna si los controladores de hardware están listos.
 */
static inline bool l298h_is_hardware_ready(const L298H_Motor_t *m)
{
    return (device_is_ready(m->in1.port) && 
            device_is_ready(m->in2.port) && 
            device_is_ready(m->pwm.dev));
}

// Configura los pines del motor al iniciar el microcontrolador
bool l298h_init(const L298H_Motor_t *motor)
{
    int ret;

    if (motor == NULL) {
        return false;
    }

    if (!l298h_is_hardware_ready(motor)) {
        return false;
    }

    /* Configurar los pines IN de control como salidas digitales */
    ret = gpio_pin_configure_dt(&motor->in1, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        return false; 
    }
    
    ret = gpio_pin_configure_dt(&motor->in2, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        return false; 
    }

    /* Asegurar que el canal PWM inicie apagado (0 nsec de pulso) */
    ret = pwm_set_pulse_dt(&motor->pwm, 0U);
    if (ret != 0) {
        return false; 
    }

    return true; 
}

// Cambia la combinación de los pines IN1 e IN2 según el sentido de giro deseado
int l298h_set_direction(const L298H_Motor_t *motor, L298H_Direction_t dir)
{
    if (motor == NULL) {
        return -EINVAL;
    }

    if (!l298h_is_hardware_ready(motor)) {
        return -ENODEV;
    }

    switch (dir) {
    case L298H_DIR_FORWARD: 
        gpio_pin_set_dt(&motor->in1, 1); 
        gpio_pin_set_dt(&motor->in2, 0); 
        break;
    case L298H_DIR_REVERSE: 
        gpio_pin_set_dt(&motor->in1, 0); 
        gpio_pin_set_dt(&motor->in2, 1); 
        break;
    case L298H_DIR_COAST: 
        gpio_pin_set_dt(&motor->in1, 0); 
        gpio_pin_set_dt(&motor->in2, 0); 
        break;
    case L298H_DIR_BRAKE: 
        gpio_pin_set_dt(&motor->in1, 1); 
        gpio_pin_set_dt(&motor->in2, 1); 
        break;
    default: 
        return -EINVAL; 
    }

    return 0; 
}

// Modifica la velocidad del motor calculando el ciclo de trabajo del PWM con escalamiento
int l298h_set_speed(const L298H_Motor_t *motor, uint8_t speed_percent)
{
    if (motor == NULL) {
        return -EINVAL;
    }

    if (!l298h_is_hardware_ready(motor)) {
        return -ENODEV;
    }

    if (speed_percent > 100U) {
        speed_percent = 100U;
    }

    // -------------------------------------------------------------------------
    // ESCALAMIENTO INDUSTRIAL: Mapea el rango 0-100% virtual al rango útil 60-100% físico
    // -------------------------------------------------------------------------
    uint8_t actual_pwm_percent = 0U;
    if (speed_percent > 0U) {
        // Fórmula de interpolación: Min_Fisico + (Vel_Virtual * (Max_Fisico - Min_Fisico) / 100)
        actual_pwm_percent = 60U + (uint32_t)(speed_percent * (100U - 60U)) / 100U;
    } else {
        actual_pwm_percent = 0U; // 0% virtual apaga el pulso por completo
    }

    uint32_t period_ns = l298h_get_period_ns(motor);
    uint32_t pulse_ns = (uint32_t)(((uint64_t)period_ns * actual_pwm_percent) / 100U);

    return pwm_set_pulse_dt(&motor->pwm, pulse_ns);
}

// Ejecuta la maniobra de parada en seco forzando al puente H
int l298h_brake(const L298H_Motor_t *motor)
{
    if (motor == NULL) {
        return -EINVAL;
    }

    if (!l298h_is_hardware_ready(motor)) {
        return -ENODEV;
    }

    /* Para frenado dinámico real, EN debe estar arriba (Habilitado) */
    uint32_t period_ns = l298h_get_period_ns(motor);
    pwm_set_pulse_dt(&motor->pwm, period_ns);

    gpio_pin_set_dt(&motor->in1, 1);
    gpio_pin_set_dt(&motor->in2, 1);

    return 0; 
}

// Apaga por completo la energía entregada al motor
int l298h_stop(const L298H_Motor_t *motor)
{
    if (motor == NULL) {
        return -EINVAL;
    }

    if (!l298h_is_hardware_ready(motor)) {
        return -ENODEV;
    }

    /* Detener el motor dejando que se detenga por fricción (Desenergizado) */
    pwm_set_pulse_dt(&motor->pwm, 0U);
    gpio_pin_set_dt(&motor->in1, 0);
    gpio_pin_set_dt(&motor->in2, 0);

    return 0; 
}