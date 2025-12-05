#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_spiffs.h"
#include "e_filament.h"


// Глобальная переменная состояния
extern state_t state;

// Вспомогательная функция для удаления пробельных символов с обоих концов строки
static char* trim_whitespace(char* str) {
	if (!str) return NULL;
    
	// Пропускаем начальные пробельные символы
	while (isspace((unsigned char)*str)) {
		str++;
	}
    
	if (*str == 0) {
		return str;
	}
    
	// Пропускаем конечные пробельные символы
	char* end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end)) {
		end--;
	}
    
	// Записываем новый конец строки
	*(end + 1) = 0;
    
	return str;
}

// Инициализация конфигурации
void config_init(void) {
	state.options_count = 0;
	memset(state.options, 0, sizeof(state.options));
}

// Функция чтения конфигурации
int config_read(const char* filename) {
	FILE* file = fopen(filename, "r");
	if (!file) {
		return -1;
	}

	char line[CONFIG_MAX_LINE_LEN];
	state.options_count = 0;

	while (fgets(line, sizeof(line), file) && state.options_count < CONFIG_MAX_OPIONS_NUM) {
		// Удаляем символы новой строки и возврата каретки
		line[strcspn(line, "\r\n")] = 0;

		// Пропускаем комментарии и пустые строки
		char* trimmed_line = trim_whitespace(line);
		if (trimmed_line[0] == '#' || trimmed_line[0] == '\0') {
			continue;
		}

		// Разделяем строку на ключ и значение
		char* delimiter = strchr(trimmed_line, ':');
		if (!delimiter) {
			continue; // Пропускаем строки без разделителя
		}

		// Разделяем ключ и значение
		*delimiter = '\0';
		char* key = trimmed_line;
		char* value = delimiter + 1;

		// Убираем пробелы вокруг ключа и значения
		key = trim_whitespace(key);
		value = trim_whitespace(value);

		// Копируем ключ и значение в структуру
		strncpy(state.options[state.options_count].key, key, CONFIG_MAX_KEY_LEN - 1);
		state.options[state.options_count].key[CONFIG_MAX_KEY_LEN - 1] = '\0';
        
		strncpy(state.options[state.options_count].value, value, CONFIG_MAX_VALUE_LEN - 1);
		state.options[state.options_count].value[CONFIG_MAX_VALUE_LEN - 1] = '\0';
        
		state.options_count++;
	}

	fclose(file);
	return state.options_count;
}

// Остальные функции остаются без изменений
const char* config_get_value(const char* key) {
	for (int i = 0; i < state.options_count; i++) {
		if (strcmp(state.options[i].key, key) == 0) {
			return state.options[i].value;
		}
	}
	return NULL;
}

int config_write(const char* filename) {
	FILE* file = fopen(filename, "w");
	if (!file) {
		return -1;
	}

	for (int i = 0; i < state.options_count; i++) {
		fprintf(file, "%s: %s\n", state.options[i].key, state.options[i].value);
	}

	fclose(file);
	return 0;
}

int config_get_int(const char* key, int default_value) {
	const char* value = config_get_value(key);
	if (value) {
		return atoi(value);
	}
	return default_value;
}

float config_get_float(const char* key, float default_value) {
	const char* value = config_get_value(key);
	if (value) {
		return atof(value);
	}
	return default_value;
}

void config_set_int(const char* key, int value) {
	for (int i = 0; i < state.options_count; i++) {
		if (strcmp(state.options[i].key, key) == 0) {
			snprintf(state.options[i].value, CONFIG_MAX_VALUE_LEN, "%d", value);
			return;
		}
	}
    
	// Если ключ не найден, добавляем новую запись
	if (state.options_count < CONFIG_MAX_OPIONS_NUM) {
		strncpy(state.options[state.options_count].key, key, CONFIG_MAX_KEY_LEN - 1);
		state.options[state.options_count].key[CONFIG_MAX_KEY_LEN - 1] = '\0';
        
		snprintf(state.options[state.options_count].value, CONFIG_MAX_VALUE_LEN, "%d", value);
		state.options_count++;
	}
}

void config_set_string(const char* key, const char* value) {
	for (int i = 0; i < state.options_count; i++) {
		if (strcmp(state.options[i].key, key) == 0) {
			strncpy(state.options[i].value, value, CONFIG_MAX_VALUE_LEN - 1);
			state.options[i].value[CONFIG_MAX_VALUE_LEN - 1] = '\0';
			return;
		}
	}
    
	// Если ключ не найден, добавляем новую запись
	if (state.options_count < CONFIG_MAX_OPIONS_NUM) {
		strncpy(state.options[state.options_count].key, key, CONFIG_MAX_KEY_LEN - 1);
		state.options[state.options_count].key[CONFIG_MAX_KEY_LEN - 1] = '\0';
        
		strncpy(state.options[state.options_count].value, value, CONFIG_MAX_VALUE_LEN - 1);
		state.options[state.options_count].value[CONFIG_MAX_VALUE_LEN - 1] = '\0';
		state.options_count++;
	}
}

void config_set_float(const char* key, float value) {
	for (int i = 0; i < state.options_count; i++) {
		if (strcmp(state.options[i].key, key) == 0) {
			snprintf(state.options[i].value, CONFIG_MAX_VALUE_LEN, "%f", value);
			return;
		}
	}
    
	// Если ключ не найден, добавляем новую запись
	if (state.options_count < CONFIG_MAX_OPIONS_NUM) {
		strncpy(state.options[state.options_count].key, key, CONFIG_MAX_KEY_LEN - 1);
		state.options[state.options_count].key[CONFIG_MAX_KEY_LEN - 1] = '\0';
        
		snprintf(state.options[state.options_count].value, CONFIG_MAX_VALUE_LEN, "%f", value);
		state.options_count++;
	}
}

void config_remove(const char* key) {
	for (int i = 0; i < state.options_count; i++) {
		if (strcmp(state.options[i].key, key) == 0) {
			// Сдвигаем все последующие элементы на одну позицию влево
			for (int j = i; j < state.options_count - 1; j++) {
				strncpy(state.options[j].key, state.options[j + 1].key, CONFIG_MAX_KEY_LEN);
				strncpy(state.options[j].value, state.options[j + 1].value, CONFIG_MAX_VALUE_LEN);
			}
			state.options_count--;
			break;
		}
	}
}