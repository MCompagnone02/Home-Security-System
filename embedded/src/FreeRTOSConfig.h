#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* Specifiche hardware per Raspberry Pi Pico RP2040 */
#define configCPU_CLOCK_HZ                      133000000
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                128
#define configTOTAL_HEAP_SIZE                   (28 * 1024)  // 28KB heap per RP2040
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_TIME_SLICING                  1

/* Configurazione base FreeRTOS */
#define configUSE_PREEMPTION                    1
#define configUSE_TICKLESS_IDLE                 0

/* Definizioni relative alle funzioni hook */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1

/* Definizioni per raccolta statistiche runtime e task */
#define configGENERATE_RUN_TIME_STATS           0

/* Funzioni opzionali */
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1

/* Definizioni specifiche Cortex-M - ottimizzate per RP2040 */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS         __NVIC_PRIO_BITS
#else
#define configPRIO_BITS         2
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY   3
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 1

#define configKERNEL_INTERRUPT_PRIORITY         ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* Configurazione assert */
#define configASSERT( x ) do { (void)(x); } while(0)

#endif /* FREERTOS_CONFIG_H */
