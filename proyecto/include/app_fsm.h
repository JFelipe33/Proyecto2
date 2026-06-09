/**
 * @file app_fsm.h
 * @brief Definiciones de la máquina de estados de la aplicación (FSM).
 */

#ifndef APP_FSM_H_
#define APP_FSM_H_

#include "l298h_api.h"
#include "sg90_api.h"
#include "lj12a3_api.h"

/* Constantes de Operación */
#define STACK_SIZE                   1024
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

typedef enum {
    STATE_SYSTEM_OFF = 0,   
    STATE_BANDA_RUNNING,    
    STATE_METAL_REJECT,     
    STATE_EMERGENCY_STOP    
} system_state_t;

typedef enum {
    EVENT_CLICK_INCREMENT, 
    EVENT_CLICK_DECREMENT, 
    EVENT_METAL_DETECTED,  
    EVENT_METAL_CLEARED,   
    EVENT_TIMEOUT,         
    EVENT_EMERGENCY,
    EVENT_BUTTON_PRESSED   // <-- NUEVO: Evento que se dispara al instante al tocar el botón
} system_event_t;

typedef struct {
    system_state_t current_state;       
    uint8_t motor_speed;                
    const L298H_Motor_t *motor;         
    const SG90_Servo_t *servo;          
    const LJ12A3_Sensor_t *sensor;      
    const struct gpio_dt_spec *user_led;
} app_fsm_t;

/* Inicializar módulo */
bool app_fsm_init(app_fsm_t *fsm, const L298H_Motor_t *motor, const SG90_Servo_t *servo, 
                  const LJ12A3_Sensor_t *sensor, const struct gpio_dt_spec *led);

/* Procesar eventos de la máquina */
void app_fsm_dispatch(system_event_t event);

/* Procesar la lógica de clics con debounce integrado */
void app_fsm_notify_button_state(bool is_pressed);

#endif /* APP_FSM_H_ */