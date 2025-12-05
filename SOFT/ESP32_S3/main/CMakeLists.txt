idf_component_register(
SRCS
	"comm.c"
	"wifi.c"
	"e_filament.c"
	"main.c"
	"color_log.c"
	"task_monitor.c"
	"config.c"
	"DRIVERS/TOUCH/touch_bsp.c"
	"DRIVERS/SH6801/ws_esp32s3_amoled_143.c"
	"DRIVERS/PCF85063A/PCF85063A.c"
	"DRIVERS/PCF85063A/timegm.c"
	"UI/images.c"
	"UI/screens.c"
	"UI/styles.c"
	"UI/ui.c"
	"UI/ui_font_rbt_lgt_150_num.c"
	"UI/ui_font_rbt_lgt_100_num.c"
	"UI/ui_font_dec160.c"
	"UI/ui_font_dec80.c"
	"UI/ui_font_rbt_lgt_60_num.c"
INCLUDE_DIRS "."
PRIV_INCLUDE_DIRS "C:/Espressif/frameworks/esp-idf-v5.4/components/esp_gdbstub/test_gdbstub_host/rv_decode/include")
component_compile_definitions(PLATFORM_ESP32)
set_source_files_properties("e_filament.c" PROPERTIES
	HEADER_FILE_ONLY OFF)
spiffs_create_partition_image(storage ../partition FLASH_IN_PROJECT)
set_source_files_properties(UI/ui_font_rbt_lgt_100.c PROPERTIES HEADER_FILE_ONLY TRUE)
