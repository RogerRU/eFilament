#pragma once

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// Максимальная длина ключа и значения
#define CONFIG_MAX_KEY_LEN 64
#define CONFIG_MAX_VALUE_LEN 128
#define CONFIG_MAX_LINE_LEN 256
#define CONFIG_MAX_OPIONS_NUM 15 // максимальное количество опций

// Структура для хранения конфигурации
typedef struct {
	char key[CONFIG_MAX_KEY_LEN];
	char value[CONFIG_MAX_VALUE_LEN];
} config_item_t;

// Глобальная структура для хранения состояния конфигурации
typedef struct {
	config_item_t options[CONFIG_MAX_OPIONS_NUM];
	int options_count;
} config_state_t;

// Инициализация конфигурации
void config_init(void);

// Функция чтения конфигурации
int config_read(const char* filename);

// Функция получения значения по ключу
const char* config_get_value(const char* key);

// Функция записи конфигурации
int config_write(const char* filename);

// Функция для получения целочисленного значения
int config_get_int(const char* key, int default_value);

// Функция для получения значения с плавающей точкой
float config_get_float(const char* key, float default_value);

// Функция для установки целочисленного значения
void config_set_int(const char* key, int value);

// Функция для установки строкового значения
void config_set_string(const char* key, const char* value);

// Функция для установки значения с плавающей точкой
void config_set_float(const char* key, float value);

// Функция для удаления опции
void config_remove(const char* key);

#endif // CONFIG_H