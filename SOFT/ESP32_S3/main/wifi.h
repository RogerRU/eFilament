
// WIFI ---------------
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
void wifi_init_sta(void);
void wifi_init_nvs(void);
bool wifi_is_connected(void);
void wifi_sntp_sync_time(void);


void start_webserver(void);
void stop_webserver(void);