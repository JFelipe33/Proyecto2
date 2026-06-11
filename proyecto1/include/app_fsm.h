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

/* Forward declaration del nuevo controlador del botón para evitar acoplamiento fuerte */
struct button_handler;

/* --- Constantes de Operación de la Planta Industrial --- */
#define STACK_SIZE                   1024U
#define MOTOR_DIR                    L298H_DIR_FORWARD  // Dirección de avance para la banda transportadora 
#define MOTOR_MAX_SPEED_PERCENT      100U 
#define MOTOR_MIN_SPEED_PERCENT      0U    // Respetando el mapeo 0-100% virtual
#define MOTOR_SPEED_STEP             25U   // Pasos escalados del 25%
#define SERVO_BASE_ANGLE             0U 
#define SERVO_DETECTION_ANGLE        60U 
#define SERVO_DETECTION_HOLD_MS      4000U 
#define EMERGENCY_BLINK_MS           100U  

/**
 * @brief Enumeración de los estados lógicos del sistema.
 */
typedef enum {
    STATE_SYSTEM_OFF = 0,   
    STATE_BANDA_RUNNING,    
    STATE_METAL_REJECT,     
    STATE_EMERGENCY_STOP,   
    STATE_MAX_STATES        
} system_state_t;

/**
 * @brief Enumeración de los eventos del sistema generados por hardware o software.
 */
typedef enum system_event_t {
    EVENT_CLICK_INCREMENT,  
    EVENT_CLICK_DECREMENT,  
    EVENT_METAL_DETECTED,   
    EVENT_METAL_CLEARED,    
    EVENT_TIMEOUT,          
    EVENT_EMERGENCY,        
    EVENT_BUTTON_PRESSED    
} system_event_t;

/**
 * @brief Estructura de contexto que almacena todo el estado físico y lógico de la aplicación.
 * @note Limpiada de las variables internas del botón que ahora residen en su propio objeto.
 */
typedef struct app_fsm {
    system_state_t current_state;               /**< Estado actual de la máquina de estados */
    uint8_t motor_speed;                        /**< Velocidad actual del motor en porcentaje */
    
    /* Punteros de control hacia los controladores de hardware y periféricos */
    const L298H_Motor_t *motor;                 
    const SG90_Servo_t *servo;                  
    const LJ12A3_Sensor_t *sensor;              
    const struct gpio_dt_spec *user_led;        
    struct button_handler *button;              /**< Enlace de comunicación bidireccional con el botón */

    /* Estructuras de sincronización y tareas diferidas internas de Zephyr RTOS */
    struct k_work sensor_work;                  /**< Tarea asíncrona para el procesamiento del sensor */
    struct k_work_delayable servo_hold_work;    /**< Temporizador para el retorno del servomotor */
    struct k_work_delayable emergency_led_work;/**< Temporizador para el parpadeo del indicador visual */
    struct k_sem sem_motor_update;             /**< Semáforo de sincronización del hilo del motor */
    struct k_thread motor_thread_data;         /**< Estructura de control del hilo del motor */
} app_fsm_t;

/* --- API Pública de Control --- */

/**
 * @brief Inicializa por completo el contexto de la máquina de estados y sus periféricos asociados.
 */
bool app_fsm_init(app_fsm_t *fsm, const L298H_Motor_t *motor, const SG90_Servo_t *servo, 
                  const LJ12A3_Sensor_t *sensor, const struct gpio_dt_spec *led, struct button_handler *button);

/**
 * @brief Procesa y despacha los eventos del sistema hacia la lógica interna de control.
 */
void app_fsm_dispatch(app_fsm_t *fsm, system_event_t event);

#endif /* APP_FSM_H_ */