#include "common.h"
#include "sound_meter.h"

static uint8_t sound_level;

uint8_t get_sound_level(void){ return sound_level; }

void update_sound_level(void) { sound_level = (uint8_t)(esp_random() % 21); }