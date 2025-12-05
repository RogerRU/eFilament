#pragma once

typedef enum {
	COLOR_MODE_FIRST_CHAR = 0,
	COLOR_MODE_WHOLE_LINE = 1
} color_mode_t;

void color_log_init(color_mode_t mode); 
