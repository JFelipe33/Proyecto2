/**
 * @file app_fsm.h
 * @brief Definiciones de la máquina de estados de la aplicación (FSM) orientada a objetos.
 */

#ifndef APP_FSM_H_
#define APP_FSM_H_

#include <zephyr/types.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include "l298h_api.h"
#include "sg90_api.h"
#include "lj12a3_api.h"

/* --- Constantes de Operación de la Planta Industrial --- */
#define STACK_SIZE                   1024U 
#define MOTOR_MAX_SPEED_PERCENT      100U 
#define MOTOR_MIN_SPEED_PERCENT      60U  
#define MOTOR_SPEED_STEP             10U
#define SERVO_BASE_ANGLE             0U 
#define SERVO_DETECTION_ANGLE        60U 
#define SERVO_DETECTION_HOLD_MS      4000U 
#define BUTTON_WINDOW_MS             2000U 
#define BUTTON_DEBOUNCE_MS           10U   
#define BUTTON_EMERGENCY_HOLD_MS     2000U 
#define EMERGENCY_BLINK_MS           100U  

/**
 * @brief Enumeración de los estados lógicos del sistema.
 */
typedef enum {
    STATE_SYSTEM_OFF = 0,   /**< Sistema detenido y en resguardo */
    STATE_BANDA_RUNNING,    /**< Banda transportadora en operaciones normales */
    STATE_METAL_REJECT,     /**< Actuador de rechazo activado por detección de metal */
    STATE_EMERGENCY_STOP,   /**< Parada crítica de seguridad activa */
    STATE_MAX_STATES        /**< Límite superior para validación de la tabla de estados */
} system_state_t;

/**
 * @brief Enumeración de los eventos del sistema generados por hardware o software.
 */
typedef enum {
    EVENT_CLICK_INCREMENT,  /**< Incremento de velocidad por pulsación única */
    EVENT_CLICK_DECREMENT,  /**< Decremento de velocidad por pulsación doble */
    EVENT_METAL_DETECTED,   /**< Presencia de material ferroso detectado por el sensor */
    EVENT_METAL_CLEARED,    /**< Pérdida de presencia de material ferroso */
    EVENT_TIMEOUT,          /**< Expiración del tiempo de expulsión del servomotor */
    EVENT_EMERGENCY,        /**< Activación física del botón de emergencia sostenido */
    EVENT_BUTTON_PRESSED    /**< Notificación inmediata de contacto físico con el botón */
} system_event_t;

/**
 * @brief Estructura de contexto que almacena todo el estado físico y lógico de la aplicación.
 * Permite la creación de múltiples instancias de bandas transportadoras de forma independiente.
 */
typedef struct app_fsm {
    system_state_t current_state;               /**< Estado actual de la máquina de estados */
    uint8_t motor_speed;                        /**< Velocidad actual del motor en porcentaje */
    
    /* Punteros de control hacia los controladores de hardware (Drivers) */
    const L298H_Motor_t *motor;                 
    const SG90_Servo_t *servo;                  
    const LJ12A3_Sensor_t *sensor;              
    const struct gpio_dt_spec *user_led;        

    /* Estructuras de sincronización y tareas diferidas internas de Zephyr RTOS */
    struct k_work sensor_work;                  /**< Tarea asíncrona para el procesamiento del sensor */
    struct k_work_delayable servo_hold_work;    /**< Temporizador para el retorno del servomotor */
    struct k_work_delayable button_window_work;/**< Ventana de tiempo para la captura de pulsaciones */
    struct k_work_delayable emergency_work;    /**< Temporizador para la validación de la parada de emergencia */
    struct k_work_delayable emergency_led_work;/**< Temporizador para el parpadeo del indicador visual */
    struct k_sem sem_motor_update;             /**< Semáforo de sincronización del hilo del motor */
    struct k_thread motor_thread_data;         /**< Estructura de control del hilo del motor */
    
    /* Variables de control protegidas para concurrencia */
    atomic_t click_count;                       /**< Contador atómico de pulsaciones del botón */
    uint32_t last_valid_time;                   /**< Registro de tiempo para el bloqueo por software (Debounce) */
} app_fsm_t;

/* --- API Pública de Control --- */

/**
 * @brief Inicializa por completo el contexto de la máquina de estados y sus periféricos asociados.
 */
bool app_fsm_init(app_fsm_t *fsm, const L298H_Motor_t *motor, const SG90_Servo_t *servo, 
                  const LJ12A3_Sensor_t *sensor, const struct gpio_dt_spec *led);

/**
 * @brief Procesa y despacha los eventos del sistema hacia la lógica interna de control.
 */
void app_fsm_dispatch(app_fsm_t *fsm, system_event_t event);

/**
 * @brief Interfaz de comunicación para reportar los cambios de estado estables del botón de usuario.
 */
void app_fsm_notify_button_state(app_fsm_t *fsm, bool is_pressed);

#endif /* APP_FSM_H_ */