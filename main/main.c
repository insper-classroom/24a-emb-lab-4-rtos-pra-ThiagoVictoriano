/*
 * LED blink with FreeRTOS
 */
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "ssd1306.h"
#include "gfx.h"

#include "pico/stdlib.h"
#include <stdio.h>

int ECHO_PIN = 13;
int TRIGGER_PIN = 12;

SemaphoreHandle_t xSemaphore_t;

QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;

void falha(){
    ssd1306_init();
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);
    
    gfx_clear_buffer(&disp);
    gfx_draw_string(&disp, 0, 0, 2, "Falha");
    gfx_show(&disp);
}

void pin_callback(uint gpio, uint32_t events){
    int time = 0;
    if (events == 0x4) { // fall edge
        time = to_us_since_boot(get_absolute_time());
    } else if (events == 0x8) { // rise edge
        time = to_us_since_boot(get_absolute_time());
    }
    xQueueSendFromISR(xQueueTime, &time, 0);
}

bool timer_0_callback(repeating_timer_t *rt) {
    xSemaphoreGiveFromISR(xSemaphore_t, 0);
    return true; // keep repeating
}

void trigger_task(void *p) {
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    printf("Iniciou Trigger\n");

    repeating_timer_t timer_0;

    if (!add_repeating_timer_ms(1000, 
                                timer_0_callback,
                                NULL, 
                                &timer_0)) {
        printf("Failed to add timer\n");
    }

    while (true) {

        if (xSemaphoreTake(xSemaphore_t, pdMS_TO_TICKS(1000)) == pdTRUE) {
            gpio_put(TRIGGER_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(10));
            gpio_put(TRIGGER_PIN, 0);
        }
  }
}

void echo_task(void *p) {
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN,
                                        GPIO_IRQ_EDGE_RISE | 
                                            GPIO_IRQ_EDGE_FALL,
                                                true,
                                                &pin_callback);
    
    int start_us = 0;
    int end_us = 0;
    int counter = 0;

    printf("Iniciou Echo\n");

    while (true) {

        if (counter == 0){
            if (xQueueReceive(xQueueTime, &start_us,  pdMS_TO_TICKS(1000))) {
                counter = 1;
            } else {
                falha();
            }
        } else {
            if (xQueueReceive(xQueueTime, &end_us,  pdMS_TO_TICKS(1000))) {
                counter = 0;
                int dist = (34300 * (end_us - start_us))/2000000;
                xQueueSend(xQueueDistance, &dist, 0);
            } else {
                falha();
            }
        }     
  }
}

void oled_task(void *p) {
    printf("Inicializando Driver\n");
    ssd1306_init();

    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    int dist;

    while (1) {
        if (xQueueReceive(xQueueDistance, &dist,  pdMS_TO_TICKS(100))) {
            gfx_clear_buffer(&disp);
            char dist_str[10]; // String temporária para armazenar o valor de dist
            sprintf(dist_str, "%d", dist); // Convertendo dist para uma string
            if (dist < 10 && dist >= 0){
                gfx_draw_string(&disp, 0, 0, 2, "Dist: ");
                gfx_draw_string(&disp, 60, 0, 2, dist_str); // Exibindo o valor de dist
                gfx_draw_string(&disp, 80, 0, 2, "cm");
            }
            else if (dist > 9 && dist < 100){
                gfx_draw_string(&disp, 0, 0, 2, "Dist: ");
                gfx_draw_string(&disp, 60, 0, 2, dist_str); // Exibindo o valor de dist
                gfx_draw_string(&disp, 90, 0, 2, "cm");
            } 
            else if (dist > 100){
                gfx_draw_string(&disp, 0, 0, 2, "Dist: ");
                gfx_draw_string(&disp, 60, 0, 2, dist_str); // Exibindo o valor de dist
                gfx_draw_string(&disp, 100, 0, 2, "cm");
            } else if (dist < 0){
                gfx_draw_string(&disp, 0, 0, 2, "N/D");
            }

            gfx_draw_line(&disp, 15, 27, dist + 15,
                          27);
            gfx_show(&disp);
        }
    }
}



int main() {
    stdio_init_all();

    xSemaphore_t = xSemaphoreCreateBinary();

    xQueueTime = xQueueCreate(32, sizeof(int) );
    xQueueDistance = xQueueCreate(32, sizeof(int) );

    xTaskCreate(oled_task, "Oled", 4095, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo", 4095, NULL, 1, NULL);
    xTaskCreate(trigger_task, "Trigger", 4095, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}
