#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

// Configurazione sistema
#define LED_RED_PIN 15      // LED Rosso - Allarme (GP15)
#define LED_GREEN_PIN 12    // LED Verde - Sistema OK (GP12)
#define LED_YELLOW_PIN 11   // LED Giallo - Armamento (GP11)
#define PIR_PIN 2           // PIR HC-SR501 sensore movimento (GP2)
#define VIBRATION_PIN 21    // SW-420 sensore vibrazione (GP21)
#define SENSOR_QUEUE_SIZE 20 // Capacità massima coda sensori

// === DEFINIZIONI TIPI ===
typedef enum {
    SECURITY_DISARMED = 0, // Sistema normale - LED Verde fisso
    SECURITY_ARMING = 1, // Countdown armamento - LED Giallo lampeggiante
    SECURITY_ARMED = 2, // Sistema armato - LED Giallo fisso
    SECURITY_ALARM = 3 // Allarme attivo - LED Rosso fisso
} security_state_t;

typedef struct {
    uint32_t timestamp; // Rilevazione movimento
    float confidence; // Livello di confidenza
    bool is_pir; // Movimento rilevato tramite PIR
    bool is_vibration; // Movimento rilevato tramite sensore di vibrazione
    uint32_t event_id; // ID progressivo
} sensor_data_t;

// Variabili globali
static QueueHandle_t sensor_queue; // Coda sensore -> controllo
static SemaphoreHandle_t system_mutex; // Mutex per stato del sitema

// Statistiche sistema
static struct {
    security_state_t current_state; // Stato corrente
    uint32_t start_time; // Timestamp avvio
} system_stats = {0};

// Dichiarazione delle funzioni utilizzate
void sensor_task(void *pvParameters);
void control_task(void *pvParameters);
void led_feedback_task(void *pvParameters);

// Controllo possibili errori di Stack Overflow
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    printf("[ERROR] Stack overflow nel task: %s\n", pcTaskName);
    // LED rosso fisso per segnalare errore
    gpio_put(LED_RED_PIN, 1);
    while(1) { sleep_ms(1000); }
}

// Controllo possibili errori durante l'allocazione di memoria
void vApplicationMallocFailedHook(void) {
    printf("[ERROR] Malloc fallito - memoria insufficiente\n");
    // LED rosso lampeggiante per segnalare errore memoria
    while(1) {
        gpio_put(LED_RED_PIN, 1);
        sleep_ms(200);
        gpio_put(LED_RED_PIN, 0);
        sleep_ms(200);
    }
}
int main() {
    // Inizializzazione comunicazione USB Serial
    stdio_usb_init();

    // Setup GPIO
    gpio_init(LED_RED_PIN); // Inizializza pin GPIO
    gpio_set_dir(LED_RED_PIN, GPIO_OUT); // Configura come output
    gpio_put(LED_RED_PIN, 0); // Spegne LED

    gpio_init(LED_GREEN_PIN); // Inizializza pin GPIO
    gpio_set_dir(LED_GREEN_PIN, GPIO_OUT); // Configura come output
    gpio_put(LED_GREEN_PIN, 0); // Spegne LED

    gpio_init(LED_YELLOW_PIN); // Inizializza pin GPIO
    gpio_set_dir(LED_YELLOW_PIN, GPIO_OUT); // Configura come output
    gpio_put(LED_YELLOW_PIN, 0); // Spegne LED

    gpio_init(PIR_PIN); // Inizializza pin GPIO
    gpio_set_dir(PIR_PIN, GPIO_IN); // Configura come input
    gpio_pull_down(PIR_PIN);  // Attiva resistenza pull-down interna

    gpio_init(VIBRATION_PIN); // Inizializza pin GPIO
    gpio_set_dir(VIBRATION_PIN, GPIO_IN); // Configura come output
    gpio_pull_down(VIBRATION_PIN);  // Attiva resistenza pull-down interna

    // Test per connessione USB con LED rosso lampeggiante
    for (int i = 0; i < 20; i++) {
        gpio_put(LED_RED_PIN, i % 2);
        sleep_ms(500);

        if (i % 5 == 0) {
            printf("Test connessione USB - Tentativo %d/20\n", i+1);
        }
    }

    printf("\nTEST CONNESSIONE USB\n");
    printf("USB Serial funziona correttamente\n");
    printf("Timestamp: %lu ms\n", to_ms_since_boot(get_absolute_time()));

    printf("\nISTEMA SICUREZZA DOMESTICA DISTRIBUITO - FREERTOS\n");

    printf("GPIO configurati:\n");
    printf("LED Rosso (Allarme): GP%d\n", LED_RED_PIN);
    printf("LED Verde (Sistema OK): GP%d\n", LED_GREEN_PIN);
    printf("LED Giallo (Armamento): GP%d\n", LED_YELLOW_PIN);
    printf("PIR Movimento: GP%d\n", PIR_PIN);
    printf("Vibrazione: GP%d\n", VIBRATION_PIN);

    // Test funzionamento LED colorati
    printf("[Test LED colorati\n");
    for(int i = 0; i < 3; i++) {
        gpio_put(LED_RED_PIN, 1);
        sleep_ms(200);
        gpio_put(LED_RED_PIN, 0);

        gpio_put(LED_YELLOW_PIN, 1);
        sleep_ms(200);
        gpio_put(LED_YELLOW_PIN, 0);

        gpio_put(LED_GREEN_PIN, 1);
        sleep_ms(200);
        gpio_put(LED_GREEN_PIN, 0);
    }

    // Inizializza strutture dati
    system_stats.start_time = 0;  // Inizializza a zero
    system_stats.current_state = SECURITY_DISARMED;  // Imposta direttamente senza mutex

    // Creazione coda FreeRTOS
    sensor_queue = xQueueCreate(SENSOR_QUEUE_SIZE, sizeof(sensor_data_t));

    // Controllo della corretta creazione della coda
    if (!sensor_queue) {
        printf("Impossibile creare la coda!\n");
        while(1) {
            gpio_put(LED_RED_PIN, 1);
            sleep_ms(1000);
            gpio_put(LED_RED_PIN, 0);
            sleep_ms(1000);
        }
    }
    // Creazione mutex: serve per proteggere l'accesso concorrente a system_stats.current_state tra control_task (scrittura) e led_feedback_task (lettura)
    system_mutex = xSemaphoreCreateMutex();

    // Controllo della corretta creazione del mutex
    if (!system_mutex) {
        printf("Impossibile creare il mutex!\n");
        while(1) {
            gpio_put(LED_YELLOW_PIN, 1);
            sleep_ms(500);
            gpio_put(LED_YELLOW_PIN, 0);
            sleep_ms(500);
        }
    }

    // Creazione Sensor task - Priorità 3 (Alta)
    if (xTaskCreate(sensor_task, "SENSOR", 1024, NULL, 3, NULL) != pdPASS) {
        printf("Fallimento SENSOR task\n");
        while(1) { sleep_ms(1000); }
    }
    printf("SENSOR task creato\n");

    // Creazione Control Task - Priorità 2 (Media)
    if (xTaskCreate(control_task, "CONTROL", 1024, NULL, 2, NULL) != pdPASS) {
        printf("Fallimento CONTROL task\n");
        while(1) { sleep_ms(1000); }
    }
    printf("CONTROL task creato\n");

    // Creazione LED feedback Task - Priorità 1 (Alta)
    if (xTaskCreate(led_feedback_task, "LED", 512, NULL, 1, NULL) != pdPASS) {
        printf("Fallimento LED task\n");
        while(1) { sleep_ms(1000); }
    }
    printf("LED task creato\n");

    // Avvia scheduler FreeRTOS - inizio task switching
    vTaskStartScheduler();

    // Se non ci sono errori questa riga non viene mai mostrata
    printf("Scheduler terminato!\n");
    return -1;
}

// Acquisizione dati dai sensori
void sensor_task(void *pvParameters) {
    printf("Sensor Task avviato\n");

    // Tempo di stabilizzazione PIR (30 secondi)
    printf("Stabilizzazione PIR in corso...\n");
    vTaskDelay(pdMS_TO_TICKS(5000));
    printf("PIR stabilizzato, avvio rilevamento\n");

    TickType_t xLastWakeTime = xTaskGetTickCount(); // Timestamp
    const TickType_t xFrequency = pdMS_TO_TICKS(1000);  // Periodo
    uint32_t event_counter = 0; // Contatore eventi
    static bool last_pir_state = false; // Stato precedente del PIR

    while (1) {
        bool pir_active = gpio_get(PIR_PIN);
        bool vib_active = gpio_get(VIBRATION_PIN);

        // Rileva solo TRANSIZIONI da 0 a 1, non stato continuo
        if ((pir_active && !last_pir_state) || vib_active) {
            printf("Movimento rilevato! PIR=%d->%d, VIB=%d\n",
                   last_pir_state, pir_active, vib_active);

            sensor_data_t sensor_data = {0};
            sensor_data.timestamp = xTaskGetTickCount();
            sensor_data.is_pir = pir_active;
            sensor_data.is_vibration = vib_active;
            sensor_data.event_id = ++event_counter;
            sensor_data.confidence = 95.0f;

            xQueueSend(sensor_queue, &sensor_data, 0); // Invio dati semza blocco
        }

        last_pir_state = pir_active;
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// Logica per il controllo
void control_task(void *pvParameters) {
    sensor_data_t sensor_data; // Buffer per ricevere i dati
    TickType_t alarm_start = 0; // Timestamp inizio allarme
    bool alarm_active = false; // Flag stato allarme

    xSemaphoreTake(system_mutex, portMAX_DELAY);
    system_stats.current_state = SECURITY_DISARMED;
    xSemaphoreGive(system_mutex);

    while (1) {
        // Se il sistema non è armato vengono accettati nuovi eventi
        if (!alarm_active && xQueueReceive(sensor_queue, &sensor_data, 0) == pdPASS) {
            // Attivazione Allarme
            printf("Nuovo Allarme - LED Rosso!\n");
            //Accesso thread-safe
            xSemaphoreTake(system_mutex, portMAX_DELAY);
            system_stats.current_state = SECURITY_ALARM;
            xSemaphoreGive(system_mutex);

            alarm_start = xTaskGetTickCount();
            alarm_active = true;
        }

        // Gestione timeout
        if (alarm_active) {
            uint32_t elapsed = (xTaskGetTickCount() - alarm_start) * portTICK_PERIOD_MS;
            if (elapsed >= 10000) {
                printf("Timeout 10s - LED Verde\n");
                xSemaphoreTake(system_mutex, portMAX_DELAY);
                system_stats.current_state = SECURITY_DISARMED;
                xSemaphoreGive(system_mutex);

                alarm_active = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Controllo LED
void led_feedback_task(void *pvParameters) {

    static security_state_t last_state = SECURITY_DISARMED;

    while (1) {
        security_state_t current_state;
        xSemaphoreTake(system_mutex, portMAX_DELAY); // Acquisizione Mutex
        current_state = system_stats.current_state; // Legge stato
        xSemaphoreGive(system_mutex); // Rilascio mutex

        // Le print vengono fatte ai cambi di stato
        if (current_state != last_state) {
            if (current_state == SECURITY_ALARM) {
                printf("LED Rosso attivo\n");
            } else {
                printf("LED Verde attivo\n");
            }
            last_state = current_state;
        }

        // Aggiorna LED
        gpio_put(LED_RED_PIN, 0);
        gpio_put(LED_GREEN_PIN, 0);
        gpio_put(LED_YELLOW_PIN, 0);

        // Gestione del LED acceso
        if (current_state == SECURITY_ALARM) {
            gpio_put(LED_RED_PIN, 1);
        } else {
            gpio_put(LED_GREEN_PIN, 1);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
