#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "esp_log.h"
#include "color_log.h"

// ANSI цвета
#define ANSI_RESET   "\033[0m"
#define ANSI_RED     "\033[0;31m"
#define ANSI_GREEN   "\033[0;32m"
#define ANSI_YELLOW  "\033[0;33m"
#define ANSI_BLUE    "\033[0;34m"
#define ANSI_CYAN    "\033[0;36m"
#define ANSI_MAGENTA "\033[0;35m"

// Режим раскраски

static color_mode_t color_mode = COLOR_MODE_FIRST_CHAR;

// Цвет по уровню
static const char* level_color(char level_char) {
	switch (level_char) {
	case 'E': return ANSI_RED;     // Error
	case 'W': return ANSI_YELLOW;  // Warning
	case 'I': return ANSI_GREEN;   // Info
	case 'D': return ANSI_CYAN;    // Debug
	case 'V': return ANSI_MAGENTA; // Verbose
	default:  return ANSI_RESET;
	}
}

static int color_log_vprintf(const char *fmt, va_list args) {
	char buf[512];
	int len = vsnprintf(buf, sizeof(buf), fmt, args);

	// Ожидаем формат вроде: "I (3260) TAG: message..."
	if (len > 3 && buf[1] == ' ' && buf[2] == '(') {
		const char *level_col = level_color(buf[0]);

		// Найдём скобку с временем
		char *time_start = buf + 3; // после "I ("
		char *time_end   = strchr(time_start, ')');
		if (!time_end) { printf("%s", buf); return len; }

		// Тег идёт после пробела после скобки
		char *tag_start = time_end + 2; // пропустить ") "
		char *tag_end   = strchr(tag_start, ':');
		if (!tag_end) { printf("%s", buf); return len; }

		if (color_mode == COLOR_MODE_FIRST_CHAR) {
			// Красим только уровень, время и тег
			printf("%s%c%s (%s%.*s%s) %s%.*s%s:%s",
				level_col,
				buf[0],
				ANSI_RESET,
				ANSI_MAGENTA,
				(int)(time_end - time_start),
				time_start,
				ANSI_RESET,
				ANSI_CYAN,
				(int)(tag_end - tag_start),
				tag_start,
				ANSI_RESET,
				tag_end + 1);
		}
		else {
			// Весь лог уровня в цвете
			printf("%s%c%s (%s%.*s%s) %s%.*s%s:%s%s%s",
				level_col,
				buf[0],
				ANSI_RESET,
				ANSI_MAGENTA,
				(int)(time_end - time_start),
				time_start,
				ANSI_RESET,
				ANSI_CYAN,
				(int)(tag_end - tag_start),
				tag_start,
				ANSI_RESET,
				level_col,
				tag_end + 1,
				ANSI_RESET);
		}
	}
	else {
		// Любой другой текст
		printf("%s", buf);
	}

	return len;
}

void color_log_init(color_mode_t mode) {
	color_mode = mode;
	esp_log_set_vprintf(color_log_vprintf);
}
