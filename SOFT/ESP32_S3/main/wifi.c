#include "e_filament.h"
#include "esp_task.h"

#include "esp_log.h"

#include "esp_sntp.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "comm.h"
#include "cJSON.h"
#include "math.h"


// Флаги для Event Group
#define WIFI_CONNECTED_BIT		BIT0
#define WIFI_FAIL_BIT			BIT1

#define EXAMPLE_ESP_MAXIMUM_RETRY 5

static int s_retry_num = 0;

EventGroupHandle_t wifi_event_group;
static EventGroupHandle_t s_wifi_event_group = NULL;
httpd_handle_t webserver_handle = NULL;
static const char *TAG = "WIFI";

extern QueueHandle_t qSend;
extern QueueHandle_t queue_update;

extern state_t state;
extern profile_t profiles[PROFILES_MAX_COUNT];

extern wifi_t wifi;

extern EventGroupHandle_t ev_states;
extern EventBits_t bits_state;
extern uint32_t touch_count;

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
	{
		esp_wifi_connect();
	}
	else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
	{
		if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
		{
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		}
		else
		{
			xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		}
		ESP_LOGI(TAG, "connect to the AP fail");
	}
	else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
	{
		ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
		ESP_LOGI("WIFI", "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
}
// --- Обработчик главной страницы (index.htm из SPIFFS) ---
esp_err_t wifi_web_index_get_handler(httpd_req_t *req)
{
	FILE *f = fopen("/storage/srv/index.html", "r");
	if (!f) {
		httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "index.htm not found");
		return ESP_FAIL;
	}
	char buf[256];
	size_t read_bytes;
	httpd_resp_set_type(req, "text/html");
	while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
		if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
			fclose(f);
			httpd_resp_sendstr_chunk(req, NULL);
			return ESP_FAIL;
		}
	}
	fclose(f);
	httpd_resp_sendstr_chunk(req, NULL);
	return ESP_OK;
}

esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t error)
{
	if (error == HTTPD_404_NOT_FOUND) {
		// Пытаемся обслужить статический файл
		
		char filepath[128]; 
		int ret = snprintf(filepath, sizeof(filepath), "/storage/srv%s", req->uri);
    
		// Проверяем, не было ли усечения
		if (ret >= sizeof(filepath)) {
			httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "URI too long");
			return ESP_FAIL;
		}

		// Проверяем, существует ли файл
		FILE *f = fopen(filepath, "r");
		if (f) {
			// Определяем Content-Type по расширению файла
			const char *content_type = "text/plain";
			const char *ext = strrchr(req->uri, '.');
			if (ext) {
				if (strcmp(ext, ".html") == 0) content_type = "text/html";
				else if (strcmp(ext, ".css") == 0) content_type = "text/css";
				else if (strcmp(ext, ".js") == 0) content_type = "application/javascript";
				else if (strcmp(ext, ".png") == 0) content_type = "image/png";
				else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) content_type = "image/jpeg";
				else if (strcmp(ext, ".gif") == 0) content_type = "image/gif";
				else if (strcmp(ext, ".ico") == 0) content_type = "image/x-icon";
			}

			httpd_resp_set_type(req, content_type);

			char buf[256];
			size_t read_bytes;
			while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
				if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
					fclose(f);
					httpd_resp_sendstr_chunk(req, NULL);
					return ESP_FAIL;
				}
			}

			fclose(f);
			httpd_resp_sendstr_chunk(req, NULL);
			return ESP_OK;
		}
	}

	// Если файл не найден, возвращаем стандартную 404 ошибку
	httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
	return ESP_FAIL;
}


// --- Обработчик загрузки файла (POST /upload) ---
esp_err_t wifi_web_upload_post_handler(httpd_req_t *req)
{
	FILE *f = fopen("/storage/eFilament.csv", "w");
	if (!f) {
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SPIFFS open error");
		return ESP_FAIL;
	}

	char buf[256];
	int received, remaining = req->content_len;
	bool file_started = false;
	size_t total_written = 0;

	while (remaining > 0) {
		received = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
		if (received <= 0) {
			fclose(f);
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
			return ESP_FAIL;
		}
		remaining -= received;

		if (!file_started) {
			char *start = strstr(buf, "\r\n\r\n");
			if (start) {
				start += 4;
				size_t data_len = received - (start - buf);
				if (data_len > 0) total_written += fwrite(start, 1, data_len, f);
				file_started = true;
			}
		}
		else {
			char *end = NULL;
			for (int i = 0; i < received - 4; i++) {
				if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '-' && buf[i + 3] == '-') {
					end = &buf[i];
					break;
				}
			}
			if (end) {
				size_t data_len = end - buf;
				if (data_len > 0) total_written += fwrite(buf, 1, data_len, f);
				break;
			}
			else {
				total_written += fwrite(buf, 1, received, f);
			}
		}
	}
	fclose(f);

	ESP_LOGI("UPLOAD", "Upload success! File size: %d bytes", (int)total_written);

	eFil_profiles_load();
	eFil_ui_roller_profiles_set(NULL, NULL);
	eFil_profile_set_active(state.profile_active_id);

	char response[128];
	snprintf(response, sizeof(response), "Upload success! File size: %d bytes", (int)total_written);
	httpd_resp_sendstr(req, response);
	return ESP_OK;
}

// Функция для отправки JSON ответа
esp_err_t send_json_response(httpd_req_t *req, cJSON *json_root) {
	
	char *json_str = cJSON_Print(json_root);
	httpd_resp_set_type(req, "application/json");
	httpd_resp_send(req, json_str, strlen(json_str));
	free(json_str);
	cJSON_Delete(json_root); 
	return ESP_OK;
}
esp_err_t responce_profile_load(httpd_req_t *req) {
	
	char buf[80];
	
	cJSON *root = cJSON_CreateObject();
	cJSON *profiles_array = cJSON_CreateArray();
    
	// Добавляем все профили в массив
	for (int i = 0; i < state.profiles_num; i++) {
		memset(buf, 0, sizeof(buf));
		sprintf(buf, "%s (%s) %s %s", profiles[i].type, profiles[i].info, profiles[i].vendor, profiles[i].info2);
		cJSON *profile_obj = cJSON_CreateObject();
		cJSON_AddStringToObject(profile_obj, "id", profiles[i].id);
		cJSON_AddStringToObject(profile_obj, "name", buf);
		cJSON_AddStringToObject(profile_obj, "type", profiles[i].type);
		cJSON_AddStringToObject(profile_obj, "info", profiles[i].info);
		cJSON_AddStringToObject(profile_obj, "vendor", profiles[i].vendor);
		cJSON_AddStringToObject(profile_obj, "info2", profiles[i].info2);
		cJSON_AddStringToObject(profile_obj, "type", profiles[i].type);
		cJSON_AddNumberToObject(profile_obj, "full_w", profiles[i].full_w);
		cJSON_AddNumberToObject(profile_obj, "spool_w", profiles[i].spool_w);
		cJSON_AddNumberToObject(profile_obj, "full_w", profiles[i].full_w);
		cJSON_AddNumberToObject(profile_obj, "density", profiles[i].density);
		cJSON_AddNumberToObject(profile_obj, "dia", profiles[i].dia);
		
		cJSON_AddItemToArray(profiles_array, profile_obj);
	}
	cJSON_AddStringToObject(root, "active_profile", state.profile_active->id);
	cJSON_AddNumberToObject(root, "active_profile_num", eFil_profile_found(state.profile_active->id));
	cJSON_AddItemToObject(root, "profiles", profiles_array);
    
	return send_json_response(req, root);
}

esp_err_t responce_profile_setactive(httpd_req_t *req) {
	char buf[80];
	int ret = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf) - 1));
	if (ret <= 0) {
		return ESP_FAIL;
	}
	buf[ret] = '\0';

	cJSON *root = cJSON_Parse(buf); // Парсим JSON
	if (root) {
		cJSON *ID = cJSON_GetObjectItem(root, "id");
		if (eFil_profile_found(ID->valuestring)) {
			eFil_profile_set_active(ID->valuestring);
			config_save();
			root = cJSON_CreateObject();
			cJSON_AddStringToObject(root, "profileId", state.profile_active_id);
			return send_json_response(req, root);
			}
		else {
			return ESP_FAIL;
		}
	}
	 return ESP_FAIL;
}


esp_err_t responce_profile_update(httpd_req_t *req) {
	static char buf[256];
	int ret = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf) - 1));
	if (ret <= 0) {
		return ESP_FAIL;
	}
	buf[ret] = '\0';

	cJSON *root = cJSON_Parse(buf); // Парсим JSON
	if (!root) {
		ESP_LOGE("cJSON", "JSON parse error!!");
		const char *resp = "{\"Status\": \"FAIL\", \"Message\": \"JSON parse error\"}";
		httpd_resp_send(req, resp, strlen(resp));
		return ESP_FAIL;
	} 
	if (root) {
		cJSON *jDATA = cJSON_GetObjectItem(root, "old_id");
	
		if (eFil_profile_found(cJSON_GetObjectItem(root, "old_id")->valuestring) != -1) {
		profile_t new_profile = {0};
			
			strncpy(new_profile.id, cJSON_GetObjectItem(root, "id")->valuestring, sizeof(new_profile.id));
			new_profile.density = cJSON_GetObjectItem(root, "density")->valuedouble;
		
			//new_profile.density = stof(cJSON_GetObjectItem(root, "density")->valuestring);
		
			new_profile.dia = cJSON_GetObjectItem(root, "diameter")->valuedouble;
			
			//new_profile.density = roundf(new_profile.density * 100) / 100.0f;
			//new_profile.dia = roundf(new_profile.dia * 100) / 100.0f;
			
			new_profile.full_w = cJSON_GetObjectItem(root, "full_weight")->valueint;
			new_profile.spool_w = cJSON_GetObjectItem(root, "spool_weight")->valueint;
			strncpy(new_profile.info, cJSON_GetObjectItem(root, "info")->valuestring, sizeof(new_profile.info));
			strncpy(new_profile.info2, cJSON_GetObjectItem(root, "info2")->valuestring, sizeof(new_profile.info2));
			strncpy(new_profile.type, cJSON_GetObjectItem(root, "type")->valuestring, sizeof(new_profile.type));
			strncpy(new_profile.vendor, cJSON_GetObjectItem(root, "vendor")->valuestring, sizeof(new_profile.vendor));
			
			eFil_profile_update(cJSON_GetObjectItem(root, "old_id")->valuestring,new_profile);
			
				
		cJSON_Delete(root);
			const char *resp = "{\"Status\": \"OK\",\"Message\": \"Update success\"}";  
		httpd_resp_send(req, resp, strlen(resp));
			return ESP_OK;
		}
		else {
			ESP_LOGE("cJSON", "Profile ID '%s' not found", jDATA->valuestring);
			cJSON_Delete(root);
			const char *resp = "{\"Status\": \"FAIL\", \"Message\": \"Update Fail! \"}";
			httpd_resp_send(req, resp, strlen(resp));
		    return ESP_FAIL;
		}
	}
	return ESP_OK;
}
esp_err_t responce_profile_add(httpd_req_t *req) {
	static char buf[256];
	int ret = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf) - 1));
	if (ret <= 0) {
		return ESP_FAIL;
	}
	buf[ret] = '\0';

	cJSON *root = cJSON_Parse(buf); // Парсим JSON
	if (!root) {
		ESP_LOGE("cJSON", "JSON parse error!!");
		const char *resp = "{\"Status\": \"FAIL\", \"Message\": \"JSON parse error\"}";
		httpd_resp_send(req, resp, strlen(resp));
		return ESP_FAIL;
	} 
			profile_t new_profile = { 0 };
			
			strncpy(new_profile.id, cJSON_GetObjectItem(root, "id")->valuestring, sizeof(new_profile.id));
			new_profile.density = cJSON_GetObjectItem(root, "density")->valuedouble;
			new_profile.dia = cJSON_GetObjectItem(root, "diameter")->valuedouble;
			new_profile.full_w = cJSON_GetObjectItem(root, "full_weight")->valueint;
			new_profile.spool_w = cJSON_GetObjectItem(root, "spool_weight")->valueint;
			strncpy(new_profile.info, cJSON_GetObjectItem(root, "info")->valuestring, sizeof(new_profile.info));
			strncpy(new_profile.info2, cJSON_GetObjectItem(root, "info2")->valuestring, sizeof(new_profile.info2));
			strncpy(new_profile.type, cJSON_GetObjectItem(root, "type")->valuestring, sizeof(new_profile.type));
			strncpy(new_profile.vendor, cJSON_GetObjectItem(root, "vendor")->valuestring, sizeof(new_profile.vendor));
			
			uint8_t result = eFil_profile_add(new_profile);
			
			if (result == 0){	
			cJSON_Delete(root);
			const char *resp = "{\"Status\": \"OK\",\"Message\": \"Add profile success!\"}";  
			httpd_resp_send(req, resp, strlen(resp));
			return ESP_OK;
				}
			if (result == 1) {
				ESP_LOGE("cJSON", "Profiles limit has been reached! (%d)", PROFILES_MAX_COUNT);
			cJSON_Delete(root);
				const char *resp = "{\"Status\": \"FAIL\", \"Message\": \"Profiles limit has been reached! \"}";
			httpd_resp_send(req, resp, strlen(resp));
			return ESP_FAIL;
			}
		if (result == 2) {
			ESP_LOGE("cJSON", "Profile ID '%s' already exist!", new_profile.id);
			cJSON_Delete(root);
			const char *resp = "{\"Status\": \"FAIL\", \"Message\": \"Profile ID already exist!\"}";
			httpd_resp_send(req, resp, strlen(resp));
			return ESP_FAIL;
		}
	return ESP_OK;
}
esp_err_t responce_profile_delete(httpd_req_t *req) {
	static char buf[256];
	int ret = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf) - 1));
	if (ret <= 0) {
		return ESP_FAIL;
	}
	buf[ret] = '\0';

	cJSON *root = cJSON_Parse(buf); // Парсим JSON
	if (!root) {
		ESP_LOGE("cJSON", "JSON parse error!!");
		const char *resp = "{\"Status\": \"FAIL\", \"Message\": \"JSON parse error\"}";
		httpd_resp_send(req, resp, strlen(resp));
		return ESP_FAIL;
	} 
				
	uint8_t result = eFil_profile_delete(cJSON_GetObjectItem(root, "id")->valuestring, true);
			
	if (result == 0) {	
		cJSON_Delete(root);
		const char *resp = "{\"Status\": \"OK\",\"Message\": \"Delete profile success!\"}";  
		httpd_resp_send(req, resp, strlen(resp));
		return ESP_OK;
	}
	if (result == 1) {
		ESP_LOGE("cJSON", "Profiles limit has been reached! (%d)", PROFILES_MAX_COUNT);
		cJSON_Delete(root);
		const char *resp = "{\"Status\": \"FAIL\", \"Message\": \"No profile ID found! \"}";
		httpd_resp_send(req, resp, strlen(resp));
		return ESP_FAIL;
	}
	if (result == 2) {
	//	ESP_LOGE("cJSON", "Profile ID '%s' already exist!", new_profile.id);
		cJSON_Delete(root);
		const char *resp = "{\"Status\": \"FAIL\", \"Message\": \"Unknown error!\"}";
		httpd_resp_send(req, resp, strlen(resp));
		return ESP_FAIL;
	}
	return ESP_OK;
}
// Обработчик прогресса калибровки нуля
esp_err_t calibrate_zero_progress_handler(httpd_req_t *req) {
	cJSON *json_root = cJSON_CreateObject();
    
	if (bits_state & STATE_BIT_CALIB_OFFSET) {
		cJSON_AddNumberToObject(json_root, "progress", state.ADC_calib_prc);
	}
	else {
		cJSON_AddNumberToObject(json_root, "progress", 100);
	}
    
	return send_json_response(req, json_root);
}
esp_err_t calibrate_zero_post_handler(httpd_req_t *req)
{
	update_t upd;
	
	ESP_LOGI("WEB SERVER", "Calibrate zero offset command arrived!");

	if (!(bits_state & (STATE_BIT_REC | STATE_BIT_CALIB_FULLSCALE | STATE_BIT_CALIB_OFFSET))) {
		bits_state = xEventGroupSetBits(ev_states, STATE_BIT_CALIB_OFFSET);

		if (xEventGroupGetBits(ev_states) & STATE_BIT_SS) {
		touch_count = 0;
		upd = UI_UPD_LOAD_SCR_MAIN;
		xQueueSend(queue_update, &upd, portMAX_DELAY);
		}
		
		upd = UI_UPD_CALIB_START;
		xQueueSend(queue_update, &upd, 0);
		
		eFil_make_packet(COMM_TYPE_COMMAND, COMM_COMMAND_ADC_START_CALIB_OFFSET, 0, 0, NULL, true);
		const char *resp = "OK";
		httpd_resp_send(req, resp, strlen(resp));

		return ESP_OK;
	}
	else {
		const char *resp = "FAIL! SYSTEM BUSY";
		httpd_resp_send(req, resp, strlen(resp));
		return ESP_FAIL;
	}
}
esp_err_t calibrate_fullscale_post_handler(httpd_req_t *req)
{
	char buf[64];
	update_t upd;
	
	if (!(bits_state & (STATE_BIT_REC | STATE_BIT_CALIB_FULLSCALE | STATE_BIT_CALIB_OFFSET))) {
		int ret = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf) - 1));
		if (ret <= 0) {
			return ESP_FAIL;
		}
		buf[ret] = '\0';

		cJSON *root = cJSON_Parse(buf); // Парсим JSON {"nominalWeight":123}
		if (root) {
			cJSON *nominalWeightItem = cJSON_GetObjectItem(root, "reference_weight");
			uint32_t reference_weight = 0;

			if (nominalWeightItem) {
				if (cJSON_IsNumber(nominalWeightItem)) {
					reference_weight = (uint32_t)nominalWeightItem->valuedouble; // Если значение - число
				}
				else if (cJSON_IsString(nominalWeightItem)) {
					reference_weight = (uint32_t)atoi(nominalWeightItem->valuestring); // Если значение - строка, преобразуем в число
				}
			}
			
			if (xEventGroupGetBits(ev_states) & STATE_BIT_SS) {
				touch_count = 0;
				upd = UI_UPD_LOAD_SCR_MAIN;
				xQueueSend(queue_update, &upd, portMAX_DELAY);
			}
						xEventGroupSetBits(ev_states, STATE_BIT_CALIB_FULLSCALE);
			bits_state = xEventGroupGetBits(ev_states);
			state.ADC_ref_weight = reference_weight;
			upd = UI_UPD_CALIB_START;
			xQueueSend(queue_update, &upd, 0);
			eFil_make_packet(COMM_TYPE_COMMAND, COMM_COMMAND_ADC_START_CALIB_FULLCASALE, reference_weight, 0, NULL, true);
			bits_state = xEventGroupSetBits(ev_states, STATE_BIT_CALIB_FULLSCALE);
			cJSON_Delete(root);
		}
		const char *resp = "OK";
		httpd_resp_send(req, resp, strlen(resp));
		return ESP_OK;
	}
	else {
		const char *resp = "FAIL! SYSTEM BUSY";
		httpd_resp_send(req, resp, strlen(resp));
		return ESP_FAIL;
	}
}

esp_err_t calibrate_fullscale_progress_handler(httpd_req_t *req) {
	cJSON *json_root = cJSON_CreateObject();
    
	if (bits_state & STATE_BIT_CALIB_FULLSCALE) {
		cJSON_AddNumberToObject(json_root, "progress", state.ADC_calib_prc);
		
	}
	else {
		cJSON_AddNumberToObject(json_root, "progress", 100);
	}
    
	return send_json_response(req, json_root);
}
esp_err_t handler_monitor(httpd_req_t *req) {
	cJSON *json_root = cJSON_CreateObject();
    
	if (state.cur_wgt != -1) {
		cJSON_AddNumberToObject(json_root, "prc", state.curr_prc/10);
		cJSON_AddNumberToObject(json_root, "prc_fl", state.curr_prc % 10);
		cJSON_AddNumberToObject(json_root, "lgt", state.cur_lgt);
		cJSON_AddNumberToObject(json_root, "wgt", state.cur_wgt);
		cJSON_AddNumberToObject(json_root, "wgt_total", state.cur_total);
	}
	else {
		cJSON_AddNumberToObject(json_root, "prc", -1);
		cJSON_AddNumberToObject(json_root, "prc_fl", 0);
		cJSON_AddNumberToObject(json_root, "lgt", 0);
		cJSON_AddNumberToObject(json_root, "wgt", 0);
		cJSON_AddNumberToObject(json_root, "wgt_total", state.cur_total);
	}
		cJSON_AddStringToObject(json_root,"active_prifile_id",state.profile_active_id);
	
	return send_json_response(req, json_root);
}

// установка опций
esp_err_t handler_profiles(httpd_req_t *req)
{
	
	char query[100];
	char type[20] = { 0 };
    
	// Получаем query string
	if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
		httpd_query_key_value(query, "type", type, sizeof(type));
	}
	
	if (strcmp(type, "setactive") == 0) {
		responce_profile_setactive(req);
		return ESP_OK;
	}
	if (strcmp(type, "load") == 0) {
		
		responce_profile_load(req);
		return ESP_OK;	
    }
	if (strcmp(type, "update") == 0) {
		
		responce_profile_update(req);
		return ESP_OK;	
	}
	if (strcmp(type, "add") == 0) {
		
		responce_profile_add(req);
		return ESP_OK;	
	}
	if (strcmp(type, "delete") == 0) {
		
		responce_profile_delete(req);
		return ESP_OK;	
	}
	return ESP_OK;
}
esp_err_t wifi_web_download_get_handler(httpd_req_t *req)
{
	FILE *f = fopen("/storage/eFilament.csv", "r");
	if (!f) {
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File not found");
		return ESP_FAIL;
	}

	// Устанавливаем заголовки для скачивания
	httpd_resp_set_type(req, "text/csv");
	httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=eFilament.csv");

	char buf[256];
	size_t read_bytes;
	while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
		if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
			fclose(f);
			httpd_resp_sendstr_chunk(req, NULL);
			return ESP_FAIL;
		}
	}

	fclose(f);
	httpd_resp_send_chunk(req, NULL, 0); // Завершаем отправку
	return ESP_OK;
}
// --- Запуск сервера ---
void start_webserver(void)
{
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.max_uri_handlers = 12;
	config.stack_size = 6144;
	

	if (httpd_start(&webserver_handle, &config) == ESP_OK) {
		httpd_uri_t index_uri = {
			.uri = "/",
			.method = HTTP_GET,
			.handler = wifi_web_index_get_handler,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(webserver_handle, &index_uri);

		httpd_register_err_handler(webserver_handle, HTTPD_404_NOT_FOUND, http_404_error_handler);
		
		httpd_uri_t upload_uri = {
			.uri = "/upload",
			.method = HTTP_POST,
			.handler = wifi_web_upload_post_handler,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(webserver_handle, &upload_uri);

		httpd_uri_t profiles_uri = {
			.uri = "/profiles",
			.method = HTTP_POST,
			.handler = handler_profiles,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(webserver_handle, &profiles_uri);

		httpd_uri_t calibrate_zero_uri = {
			.uri = "/calibrate_zero",
			.method = HTTP_POST,
			.handler = calibrate_zero_post_handler,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(webserver_handle, &calibrate_zero_uri);

		httpd_uri_t calibrate_fullscale_uri = {
			.uri = "/calibrate_fullscale",
			.method = HTTP_POST,
			.handler = calibrate_fullscale_post_handler,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(webserver_handle, &calibrate_fullscale_uri);

		// URI для скачивания файла профайлов
		httpd_uri_t download_uri = {
			.uri = "/download",
			.method = HTTP_GET,
			.handler = wifi_web_download_get_handler,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(webserver_handle, &download_uri);
		
		const httpd_uri_t calibrate_zero_progress = {
			.uri = "/calibrate_zero_progress",
			.method = HTTP_GET,
			.handler = calibrate_zero_progress_handler,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(webserver_handle, &calibrate_zero_progress);
		
		const httpd_uri_t calibrate_fullscale_progress = {
			.uri = "/calibrate_fullscale_progress",
			.method = HTTP_GET,
			.handler = calibrate_fullscale_progress_handler,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(webserver_handle, &calibrate_fullscale_progress);
		
		const httpd_uri_t monitor = {
			.uri = "/monitor",
			.method = HTTP_GET,
			.handler = handler_monitor,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(webserver_handle, &monitor);
		

		ESP_LOGI("HTTP", "Webserver started");
	}
}
void stop_webserver(void)
{
	if (webserver_handle) {
		httpd_stop(webserver_handle);
		webserver_handle = NULL;
		ESP_LOGI("HTTP", "Webserver stopped");
	}
}

void wifi_init_nvs()
{
	ESP_LOGI("NVS", "Start initialization....");
	esp_err_t ret = nvs_flash_init();

	switch (ret)
	{
	case ESP_OK:
		ESP_LOGI("NVS", "Initialized successfully");
		break;

	case ESP_ERR_NVS_NO_FREE_PAGES:
	case ESP_ERR_NVS_NEW_VERSION_FOUND:
		ESP_LOGW("NVS", "Requires erase (reason: 0x%X)", ret);
		ESP_ERROR_CHECK(nvs_flash_erase());
		ESP_ERROR_CHECK(nvs_flash_init());
		ESP_LOGI("NVS", "Reinitialized after erase");
		break;

	default:
		ESP_LOGE("NVS", "Fatal error: 0x%X", ret);
		abort();
	}
}
void wifi_init_sta()
{
	if (s_wifi_event_group == NULL)
	{
		s_wifi_event_group = xEventGroupCreate();
		if (s_wifi_event_group == NULL)
		{
			ESP_LOGE("WIFI", "Failed to create Event Group!");
			abort();
		}
	}

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	cfg.static_rx_buf_num = 4;		// Было 10 (по умолчанию)
	cfg.dynamic_rx_buf_num = 2;		// Было 32
	cfg.tx_buf_type = 0;			// 1 = Dynamic TX buffers (меньше DMA)
	cfg.rx_ba_win = 2;				// Окно агрегации

	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
		ESP_EVENT_ANY_ID,
		&wifi_event_handler,
		NULL,
		&instance_any_id));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
		IP_EVENT_STA_GOT_IP,
		&wifi_event_handler,
		NULL,
		&instance_got_ip));

	wifi_config_t wifi_config = {
		.sta = {
		.ssid = "",
		.password = "",
		.threshold.authmode = WIFI_AUTH_WPA2_PSK,
	},
	};
	if (wifi.hasPass)
	{
		strncpy((char *)wifi_config.sta.ssid, wifi.ssid, sizeof(wifi_config.sta.ssid));
		strncpy((char *)wifi_config.sta.password, wifi.pass, sizeof(wifi_config.sta.password));
	}
	else
	{
		ESP_LOGW("WIFI", "WiFi SSID/PASS not set!");
	}

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI("MEM",
		"Free heap: %d, Min free: %d",
		(int)esp_get_free_heap_size(),
		(int)esp_get_minimum_free_heap_size());

	ESP_LOGI("WIFI", "wifi_init_sta finished.");

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
		WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
		pdFALSE,
		pdFALSE,
		pdMS_TO_TICKS(10000));

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT)
	{
		ESP_LOGI("WIFI", "connected to ap SSID:%s password:%s", wifi.ssid, wifi.pass);
	}
	else if (bits & WIFI_FAIL_BIT)
	{
		ESP_LOGI("WIFI", "Failed to connect to SSID:%s, password:%s", wifi.ssid, wifi.pass);
	}
	else
	{
		ESP_LOGE("WIFI", "UNEXPECTED EVENT");
	}
}
bool wifi_is_connected(void)
{
	EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
	return (bits & WIFI_CONNECTED_BIT) != 0;
}

void wifi_sntp_sync_time(void)
{
	setenv("TZ", "UTC-3", 1);
	tzset();

	esp_sntp_stop(); // на всякий случай
	esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
	esp_sntp_setservername(0, "pool.ntp.org");
	esp_sntp_init();

	// Ждём синхронизации времени (до 10 попыток)
	time_t now = 0;
	struct tm timeinfo = { 0 };
	int retry = 10;
	while (retry-- && timeinfo.tm_year < (2020 - 1900)) {
		vTaskDelay(2000 / portTICK_PERIOD_MS);
		time(&now);
		localtime_r(&now, &timeinfo);
	}
	if (timeinfo.tm_year >= (2020 - 1900)) {
		ESP_LOGI("SNTP", "Time sync: %04d-%02d-%02d %02d:%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
		time_set_system(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
		wifi.SNTP_synchronized = true;
		time_set_to_PCF();
	}
	else {
		ESP_LOGE("SNTP", "Failed to sync time");
		wifi.SNTP_synchronized = false;
	}
}