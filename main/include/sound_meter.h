#ifndef SOUND_METER_H
#define SOUND_METER_H

#include "esp_random.h"

#define SOUND_METER_TASK_PERIOD (1000 / portTICK_PERIOD_MS)

uint8_t get_sound_level(void);
void update_sound_level(void);

#endif