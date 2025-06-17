/* HTTP Restful API Server
 *
 * This example code is in the Public Domain.
 */

#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include "esp_http_server.h"
#include "esp_chip_info.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "cJSON.h"
#include "74AHC595.h"
#include "main.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mqtt_com.h"
#include "ota.h"

const char *CONFIG_TAG = "CONFIG";

extern SemaphoreHandle_t xNewDataSemaphore;

extern status_t status;  // Global device status
extern volatile int ota_progress;


// Helper function to parse a separator string to an enum value
enum pp_separator_t parse_separator(const char *sep_str)
{
    if (!sep_str) return SEP_NULL;
    if (strcasecmp(sep_str, "colon") == 0) return SEP_COLON;
    else if (strcasecmp(sep_str, "space") == 0) return SEP_SPACE;
    else if (strcasecmp(sep_str, "blank") == 0) return SEP_BLANK;
    else if (strcasecmp(sep_str, "dot") == 0) return SEP_DOT;
    else if (strcasecmp(sep_str, "dash") == 0) return SEP_DASH;
    return SEP_NULL;
}

// Helper function to convert a separator enum value to a string
const char *separator_to_string(enum pp_separator_t sep)
{
    switch(sep){
        case SEP_COLON: return "colon";
        case SEP_SPACE: return "space";
        case SEP_BLANK: return "blank";
        case SEP_DOT: return "dot";
        case SEP_DASH: return "dash";
        default: return NULL; // For SEP_NULL, return NULL so that JSON gets a null value
    }
}

// Helper function to parse a mode string to an enum value
enum pp_mode_t parse_mode(const char *mode_str)
{
    if (!mode_str) return MODE_NONE;
    if (strcasecmp(mode_str, "none") == 0) return MODE_NONE;
    else if (strcasecmp(mode_str, "mqtt") == 0) return MODE_MQTT;
    else if (strcasecmp(mode_str, "timer") == 0) return MODE_TIMER;
    else if (strcasecmp(mode_str, "clock") == 0) return MODE_CLOCK;
    else if (strcasecmp(mode_str, "manual") == 0) return MODE_MANUAL;
    else if (strcasecmp(mode_str, "custom-api") == 0) return MODE_CUSTOM_API;
    return MODE_NONE;
}

// Helper function to convert a mode enum value to a string
const char *mode_to_string(enum pp_mode_t mode)
{
    switch(mode){
        case MODE_NONE: return "none";
        case MODE_MQTT: return "mqtt";
        case MODE_TIMER: return "timer";
        case MODE_CLOCK: return "clock";
        case MODE_MANUAL: return "manual";
        case MODE_CUSTOM_API: return "custom-api";
        default: return "none";
    }
}

static const char *REST_TAG = "REST";
#define REST_CHECK(a, str, goto_tag, ...)                                         \
    do {                                                                          \
        if (!(a)) {                                                               \
            ESP_LOGE(REST_TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                        \
        }                                                                         \
    } while (0)

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

#define CHECK_FILE_EXTENSION(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
    const char *type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "text/xml";
    }
    return httpd_resp_set_type(req, type);
}

/* Send HTTP response with the contents of the requested file from SPIFFS */
static esp_err_t rest_common_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
    strlcpy(filepath, rest_context->base_path, sizeof(filepath));
    if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "/index.html", sizeof(filepath));
    } else {
        strlcat(filepath, req->uri, sizeof(filepath));
    }
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        ESP_LOGE(REST_TAG, "Failed to open file : %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }
    set_content_type_from_file(req, filepath);

    char *chunk = rest_context->scratch;
    ssize_t read_bytes;
    do {
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(REST_TAG, "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(REST_TAG, "File sending failed!");
                httpd_resp_sendstr_chunk(req, NULL);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    close(fd);
    ESP_LOGI(REST_TAG, "File sending complete");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// Handler for POST /api/v1/config
// Accepts a JSON object with the structure:
// {
//   "general": { "groups": number, "led": true/false },
//   "groups": {
//       "group0": { "start_position": x, "end_position": y, "pattern": { "disp0": val, ... }, "separator": "colon" or null, "mode": "mqtt"/"none"/... [, additional options] },
//       "group1": { ... },
//       ...
//    }
// }
static esp_err_t config_post_handler(httpd_req_t *req)
{
    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
    int total_len = req->content_len;
    int cur_len = 0, received = 0;
    char *buf = rest_context->scratch;
    
    LED_set_color(BLUE, 1);

    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }

    // Receive the request data in chunks
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[cur_len] = '\0';

    // Parse the JSON object
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(CONFIG_TAG, "Invalid JSON");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Parse "general" section
    cJSON *general = cJSON_GetObjectItem(root, "general");
    if (general && cJSON_IsObject(general)) {
        cJSON *groups_item = cJSON_GetObjectItem(general, "groups");
        if (groups_item && cJSON_IsNumber(groups_item)) {
            status.total_groups = groups_item->valueint;
        }
//        cJSON *led_item = cJSON_GetObjectItem(general, "led");
//        if (led_item && (cJSON_IsBool(led_item) || cJSON_IsNumber(led_item))) {
//            status.led = (cJSON_IsTrue(led_item) || (led_item->valueint != 0)) ? 1 : 0;
//        }
        cJSON *time_zone_item = cJSON_GetObjectItem(general, "time_zone");
		if (time_zone_item && cJSON_IsString(time_zone_item) && time_zone_item->valuestring) {
		    strncpy(status.timezone, time_zone_item->valuestring, sizeof(status.timezone) - 1);
		    status.timezone[sizeof(status.timezone)-1] = '\0';
		}
    } else {
        ESP_LOGW(CONFIG_TAG, "Missing 'general' section");
    }

    // Parse "groups" section
    cJSON *groups = cJSON_GetObjectItem(root, "groups");
    if (groups && cJSON_IsObject(groups)) {
        cJSON *group = NULL;
        cJSON_ArrayForEach(group, groups) {
            if (!group->string) continue;
            int group_index = -1;
            if (sscanf(group->string, "group%d", &group_index) != 1) {
                ESP_LOGW(CONFIG_TAG, "Invalid group name: %s", group->string);
                continue;
            }
            if (group_index < 0 || group_index >= MAX_GROUPS) continue;

            // Parse start_position and end_position
            cJSON *start_item = cJSON_GetObjectItem(group, "start_position");
            cJSON *end_item = cJSON_GetObjectItem(group, "end_position");
            if (start_item && cJSON_IsNumber(start_item))
                status.groups[group_index].start_position = start_item->valueint;
            if (end_item && cJSON_IsNumber(end_item))
                status.groups[group_index].end_position = end_item->valueint;

            // Parse the "pattern" object
            cJSON *pattern_obj = cJSON_GetObjectItem(group, "pattern");
            if (pattern_obj && cJSON_IsObject(pattern_obj)) {
                // Reset the pattern array
                for (int i = 0; i < MAX_DISPLAYS; i++) {
                    status.groups[group_index].pattern[i] = 0;
                }
                cJSON *disp = NULL;
                cJSON_ArrayForEach(disp, pattern_obj) {
                    if (!disp->string) continue;
                    int disp_index = -1;
                    if (sscanf(disp->string, "disp%d", &disp_index) != 1) continue;
                    if (disp_index >= 0 && disp_index < MAX_DISPLAYS && cJSON_IsNumber(disp)) {
                        status.groups[group_index].pattern[disp_index] = disp->valueint;
                    }
                }
            }

            // Parse the separator field using parse_separator helper
            cJSON *sep_item = cJSON_GetObjectItem(group, "separator");
            if (sep_item) {
                if (cJSON_IsString(sep_item)) {
                    status.groups[group_index].separator = parse_separator(sep_item->valuestring);
                } else {
                    // If separator is null or not a string, set to SEP_NULL
                    status.groups[group_index].separator = SEP_NULL;
                }
            } else {
                status.groups[group_index].separator = SEP_NULL;
            }

            // Parse the "mode" field using parse_mode helper
            cJSON *mode_item = cJSON_GetObjectItem(group, "mode");
            if (mode_item && cJSON_IsString(mode_item)) {
                status.groups[group_index].mode = parse_mode(mode_item->valuestring);
            } else {
                status.groups[group_index].mode = MODE_NONE;
            }

            // Parse additional options based on mode
            //MQTT
            if (status.groups[group_index].mode == MODE_MQTT) {
                cJSON *topic_item = cJSON_GetObjectItem(group, "topic");
                if (topic_item && cJSON_IsString(topic_item)) {
                    strncpy(status.groups[group_index].mqtt.topic, topic_item->valuestring, sizeof(status.groups[group_index].mqtt.topic) - 1);
                }
            }
            //Timer
            if (status.groups[group_index].mode == MODE_TIMER) {
			    cJSON *type_item = cJSON_GetObjectItem(group, "type");
			    if (type_item && cJSON_IsString(type_item)) {
			        const char *type = type_item->valuestring;
			        if (strcmp(type, "advanced") == 0) {
			            status.groups[group_index].timer.type = TIMER_ADVANCED;
			
			            // cycles (string → int)
			            cJSON *cycles_item = cJSON_GetObjectItem(group, "cycles");
			            if (cycles_item && cJSON_IsNumber(cycles_item)) {
			                status.groups[group_index].timer.cycles = cycles_item->valueint;
			            }
			
			            // showCurrentCycle (bool)
			            cJSON *show_item = cJSON_GetObjectItem(group, "showCurrentCycle");
			            if (show_item && cJSON_IsBool(show_item)) {
			                status.groups[group_index].timer.show_curr_cycle = cJSON_IsTrue(show_item);
			            }
			
			            // work (number)
			            cJSON *work_item = cJSON_GetObjectItem(group, "work");
			            if (work_item && cJSON_IsNumber(work_item)) {
			                status.groups[group_index].timer.work_time = work_item->valueint;
			            }
			
			            // rest (number)
			            cJSON *rest_item = cJSON_GetObjectItem(group, "rest");
			            if (rest_item && cJSON_IsNumber(rest_item)) {
			                status.groups[group_index].timer.rest_time = rest_item->valueint;
			            }
			
			        } else if (strcmp(type, "simple") == 0) {
			            status.groups[group_index].timer.type = TIMER_SIMPLE;
			
			            // from (number)
			            cJSON *from_item = cJSON_GetObjectItem(group, "from");
			            if (from_item && cJSON_IsNumber(from_item)) {
			                status.groups[group_index].timer.count_from = from_item->valueint;
			            }
			
			            // to (number)
			            cJSON *to_item = cJSON_GetObjectItem(group, "to");
			            if (to_item && cJSON_IsNumber(to_item)) {
			                status.groups[group_index].timer.count_to = to_item->valueint;
			            }
			
			        } else {
			            // Unknown type
			            status.groups[group_index].timer.type = TIMER_NONE;
			        }
			        
			        //Common section
		            // intervalUnit
					cJSON *iunit_item = cJSON_GetObjectItem(group, "intervalUnit");
					if (iunit_item && cJSON_IsString(iunit_item) && iunit_item->valuestring) {
					    const char *unit = iunit_item->valuestring;
				        if      (unit[0] == 's') status.groups[group_index].timer.interval_unit = INTERVAL_SECONDS;
				        else if (unit[0] == 'm') status.groups[group_index].timer.interval_unit = INTERVAL_MINUTES;
				        else if (unit[0] == 'h') status.groups[group_index].timer.interval_unit = INTERVAL_HOURS;
				        else if (unit[0] == 'd') status.groups[group_index].timer.interval_unit = INTERVAL_DAYS;
				        else                      status.groups[group_index].timer.interval_unit = INTERVAL_SECONDS; // fallback
				     }
				     // alarm (bool)
		            cJSON *alarm_item = cJSON_GetObjectItem(group, "alarm");
		            if (alarm_item && cJSON_IsBool(alarm_item)) {
		                status.groups[group_index].timer.alarm = cJSON_IsTrue(alarm_item);
		            }
				     		        
			    } else {
			        // Lack of "type"
			        status.groups[group_index].timer.type = TIMER_NONE;
			    }
			}
			// Clock
			if (status.groups[group_index].mode == MODE_CLOCK) {
			    // clock_unit → pp_clock_type_t
			    cJSON *unit_item = cJSON_GetObjectItem(group, "clock_unit");
			    if (unit_item && cJSON_IsString(unit_item)) {
			        const char *u = unit_item->valuestring;
			        if      (strcmp(u, "seconds") == 0) status.groups[group_index].clock.type = RTC_SECONDS;
			        else if (strcmp(u, "minutes") == 0) status.groups[group_index].clock.type = RTC_MINUTES;
			        else if (strcmp(u, "hours")   == 0) status.groups[group_index].clock.type = RTC_HOURS;
			        else if (strcmp(u, "days")     == 0) status.groups[group_index].clock.type = RTC_DAY;
			        else if (strcmp(u, "months")   == 0) status.groups[group_index].clock.type = RTC_MONTCH;
			        else if (strcmp(u, "years")    == 0) status.groups[group_index].clock.type = RTC_YEAR;
			        else                                status.groups[group_index].clock.type = RTC_NONE;
			    } else {
			        status.groups[group_index].clock.type = RTC_NONE;
			    }
			
			    // time_format → pp_time_format_t + bool flag
			    cJSON *fmt_item = cJSON_GetObjectItem(group, "time_format");
			    if (fmt_item && cJSON_IsString(fmt_item)) {
			        if (strcmp(fmt_item->valuestring, "12h") == 0) {
			            status.groups[group_index].clock.time_format = FORMAT_12H;
			            //status.groups[group_index].clock.time_tormat = false;
			        } else {
			            // domyślnie 24h
			            status.groups[group_index].clock.time_format = FORMAT_24H;
			            //status.groups[group_index].clock.time_tormat = true;
			        }
			    } else {
			        // brak pola – użyj 24-godzinnego
			        status.groups[group_index].clock.time_format = FORMAT_24H;
			        //status.groups[group_index].clock.time_tormat = true;
			    }
			
			    // offset → time_offset
			    cJSON *off_item = cJSON_GetObjectItem(group, "offset");
			    if (off_item && cJSON_IsNumber(off_item)) {
			        status.groups[group_index].clock.time_offset = off_item->valueint;
			    } else {
			        status.groups[group_index].clock.time_offset = 0;
			    }
			}
			// CUSTOM-API
			if (status.groups[group_index].mode == MODE_CUSTOM_API) {
			    // endpoint → url
			    cJSON *endpoint_item = cJSON_GetObjectItem(group, "endpoint");
			    if (endpoint_item && cJSON_IsString(endpoint_item) && endpoint_item->valuestring) {
			        strncpy(status.groups[group_index].api.url,
			                endpoint_item->valuestring,
			                sizeof(status.groups[group_index].api.url) - 1);
			        status.groups[group_index].api.url[
			            sizeof(status.groups[group_index].api.url) - 1] = '\0';
			    }
			
			    // method → pp_rest_method_t
			    cJSON *method_item = cJSON_GetObjectItem(group, "method");
			    if (method_item && cJSON_IsString(method_item) && method_item->valuestring) {
			        if (strcmp(method_item->valuestring, "GET") == 0) {
			            status.groups[group_index].api.method = GET;
			        } else {
			            status.groups[group_index].api.method = POST;
			        }
			    }
			
			    // headers → serialize JSON object back to string
			    cJSON *headers_item = cJSON_GetObjectItem(group, "headers");
			    if (headers_item && cJSON_IsObject(headers_item)) {
			        char *hdr_str = cJSON_PrintUnformatted(headers_item);
			        if (hdr_str) {
			            strncpy(status.groups[group_index].api.headers,
			                    hdr_str,
			                    sizeof(status.groups[group_index].api.headers) - 1);
			            status.groups[group_index].api.headers[
			                sizeof(status.groups[group_index].api.headers) - 1] = '\0';
			            cJSON_free(hdr_str);
			        }
			    } else {
			        status.groups[group_index].api.headers[0] = '\0';
			    }
			
			    // responseFormat → pp_response_format_t
			    cJSON *fmt_item = cJSON_GetObjectItem(group, "responseFormat");
			    if (fmt_item && cJSON_IsString(fmt_item) && fmt_item->valuestring) {
			        if      (strcmp(fmt_item->valuestring, "xml")  == 0) status.groups[group_index].api.format = XML;
			        else if (strcmp(fmt_item->valuestring, "text") == 0) status.groups[group_index].api.format = TEXT;
			        else                                                  status.groups[group_index].api.format = JSON;
			    }
			
			    // responseKeyPath → key_patch
			    cJSON *key_item = cJSON_GetObjectItem(group, "responseKeyPath");
			    if (key_item && cJSON_IsString(key_item) && key_item->valuestring) {
			        strncpy(status.groups[group_index].api.key_patch,
			                key_item->valuestring,
			                sizeof(status.groups[group_index].api.key_patch) - 1);
			        status.groups[group_index].api.key_patch[
			            sizeof(status.groups[group_index].api.key_patch) - 1] = '\0';
			    }
			
			    // interval → pulling_interval (number)
			    cJSON *int_item = cJSON_GetObjectItem(group, "interval");
			    if (int_item && cJSON_IsNumber(int_item)) {
			        status.groups[group_index].api.pulling_interval = (uint8_t)int_item->valueint;
			    }
			}
						
            
        }
    } else {
        ESP_LOGW(CONFIG_TAG, "Missing 'groups' section");
    }
    
    // If the object is received from the web application, pass it for handling
    xSemaphoreGive(xNewDataSemaphore);
      
    /* Always close the socket This prevents delays caused by the application trying to connect through an already closed socket.
    Now the socket will be assigned anew each time */
    cJSON_Delete(root);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, "Config updated successfully");
    return ESP_OK;
}

// Handler for GET /api/v1/config
// Returns a JSON object based on the current configuration stored in the global 'status' structure
static esp_err_t config_get_handler(httpd_req_t *req)
{
    // 1. Stwórz korzeń dokumentu
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_FAIL;
    }

    // 2. Sekcja "general"
    cJSON *general = cJSON_CreateObject();
    cJSON_AddNumberToObject(general, "groups", status.total_groups);
    // status.display_number traktujemy jako "modules" (zgodnie z przykładem)
    cJSON_AddNumberToObject(general, "modules", status.display_number);
//    cJSON_AddBoolToObject(general, "led", status.led ? true : false);
    cJSON_AddStringToObject(general, "time_zone", status.timezone);
    cJSON_AddItemToObject(root, "general", general);

    // 3. Sekcja "groups"
    cJSON *groups = cJSON_CreateObject();
    for (int i = 0; i < status.total_groups && i < MAX_GROUPS; i++) {
        char grp_name[16];
        snprintf(grp_name, sizeof(grp_name), "group%d", i);
        cJSON *g = cJSON_CreateObject();

        // pozycje
        cJSON_AddNumberToObject(g, "start_position", status.groups[i].start_position);
        cJSON_AddNumberToObject(g, "end_position",   status.groups[i].end_position);

        // pattern
        cJSON *pattern = cJSON_CreateObject();
        for (int j = 0; j < MAX_DISPLAYS; j++) {
            if (status.groups[i].pattern[j] != 0) {
                char disp_name[16];
                snprintf(disp_name, sizeof(disp_name), "disp%d", j);
                cJSON_AddNumberToObject(pattern, disp_name, status.groups[i].pattern[j]);
            }
        }
        cJSON_AddItemToObject(g, "pattern", pattern);

        // separator
        if (status.groups[i].separator == SEP_NULL) {
            cJSON_AddItemToObject(g, "separator", cJSON_CreateNull());
        } else {
            const char *sep_str = "null";
            switch (status.groups[i].separator) {
                case SEP_SPACE: sep_str = "space"; break;
                case SEP_BLANK: sep_str = "blank"; break;
                case SEP_COLON: sep_str = "colon"; break;
                case SEP_DOT:   sep_str = "dot";   break;
                case SEP_DASH:  sep_str = "dash";  break;
                default: break;
            }
            cJSON_AddStringToObject(g, "separator", sep_str);
        }

        // mode
        const char *mode_str = "none";
        switch (status.groups[i].mode) {
            case MODE_MQTT:       mode_str = "mqtt";       break;
            case MODE_TIMER:      mode_str = "timer";      break;
            case MODE_CLOCK:      mode_str = "clock";      break;
            case MODE_MANUAL:     mode_str = "manual";     break;
            case MODE_CUSTOM_API: mode_str = "custom-api"; break;
            default: break;
        }
        cJSON_AddStringToObject(g, "mode", mode_str);

        // pola dodatkowe w zależności od trybu
        if (status.groups[i].mode == MODE_MQTT) {
            cJSON_AddStringToObject(g, "topic", status.groups[i].mqtt.topic);
        }
        else if (status.groups[i].mode == MODE_TIMER) {
            // wspólne
            cJSON_AddNumberToObject(g, "intervalValue", status.groups[i].timer.interval);
            const char *unit_str = "s";
            switch (status.groups[i].timer.interval_unit) {
                case INTERVAL_SECONDS: unit_str = "s"; break;
                case INTERVAL_MINUTES: unit_str = "m"; break;
                case INTERVAL_HOURS:   unit_str = "h"; break;
                case INTERVAL_DAYS:    unit_str = "d"; break;
                default: break;
            }
            cJSON_AddStringToObject(g, "intervalUnit", unit_str);
            cJSON_AddBoolToObject(g, "alarm", status.groups[i].timer.alarm);

            if (status.groups[i].timer.type == TIMER_ADVANCED) {
                cJSON_AddStringToObject(g, "type", "advanced");
                cJSON_AddNumberToObject(g, "cycles",           status.groups[i].timer.cycles);
                cJSON_AddBoolToObject(g,   "showCurrentCycle", status.groups[i].timer.show_curr_cycle);
                cJSON_AddNumberToObject(g, "work",             status.groups[i].timer.work_time);
                cJSON_AddNumberToObject(g, "rest",             status.groups[i].timer.rest_time);
            }
            else if (status.groups[i].timer.type == TIMER_SIMPLE) {
                cJSON_AddStringToObject(g, "type", "simple");
                cJSON_AddNumberToObject(g, "from", status.groups[i].timer.count_from);
                cJSON_AddNumberToObject(g, "to",   status.groups[i].timer.count_to);
            }
            else {
                cJSON_AddStringToObject(g, "type", "none");
            }
        }
        else if (status.groups[i].mode == MODE_CLOCK) {
            // clock_unit
            const char *cu = "none";
            switch (status.groups[i].clock.type) {
                case RTC_SECONDS: cu = "seconds"; break;
                case RTC_MINUTES: cu = "minutes"; break;
                case RTC_HOURS:   cu = "hours";   break;
                case RTC_DAY:     cu = "day";     break;
                case RTC_MONTCH:  cu = "month";   break;
                case RTC_YEAR:    cu = "year";    break;
                default: break;
            }
            cJSON_AddStringToObject(g, "clock_unit", cu);

            // time_format
            cJSON_AddStringToObject(
                g,
                "time_format",
                status.groups[i].clock.time_format == FORMAT_12H ? "12h" : "24h"
            );

            // offset
            cJSON_AddNumberToObject(g, "offset", status.groups[i].clock.time_offset);
        }
        else if (status.groups[i].mode == MODE_CUSTOM_API) {
            cJSON_AddStringToObject(g, "endpoint", status.groups[i].api.url);
            cJSON_AddStringToObject(
                g,
                "method",
                status.groups[i].api.method == GET ? "GET" : "POST"
            );
            // headers (parsuj z zapisanego stringa lub twórz pusty obiekt)
            cJSON *hdrs = NULL;
            if (status.groups[i].api.headers[0]) {
                hdrs = cJSON_Parse(status.groups[i].api.headers);
            }
            if (!hdrs) {
                hdrs = cJSON_CreateObject();
            }
            cJSON_AddItemToObject(g, "headers", hdrs);

            // body zawsze null (nie obsługujesz zapisu body w status)
            cJSON_AddItemToObject(g, "body", cJSON_CreateNull());

            // responseFormat
            const char *rf = "json";
            if (status.groups[i].api.format == XML)  rf = "xml";
            if (status.groups[i].api.format == TEXT) rf = "text";
            cJSON_AddStringToObject(g, "responseFormat", rf);

            cJSON_AddStringToObject(g, "responseKeyPath", status.groups[i].api.key_patch);
            cJSON_AddNumberToObject(g, "interval", status.groups[i].api.pulling_interval);
        }

        cJSON_AddItemToObject(groups, grp_name, g);
    }
    cJSON_AddItemToObject(root, "groups", groups);

    // 4. Wyślij odpowiedź
    char *response = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, response);

    // 5. Sprzątanie
      
    cJSON_free(response);
    cJSON_Delete(root);

    return ESP_OK;
}

// Handler for GET /api/v1/versions endpoint.
static esp_err_t versions_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    char firm_version[32] = {0};
    char web_app_version[32] = {0};
    nvs_handle_t nvs;
    if(nvs_open("storage", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(firm_version);
		esp_err_t err = nvs_get_str(nvs, "firm_version", firm_version, &len);
		if (err != ESP_OK) {
		    ESP_LOGE(REST_TAG, "Failed to get firm_version from NVS, error = %d", err);
		    strcpy(firm_version, "unknown");
		}
        len = sizeof(web_app_version);
        if(nvs_get_str(nvs, "web_app_version", web_app_version, &len) != ESP_OK) {
            strcpy(web_app_version, "unknown");
        }
        nvs_close(nvs);
    } else {
        strcpy(firm_version, "unknown");
        strcpy(web_app_version, "unknown");
    }

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"versions\":{\"firm_version\":\"%s\",\"web_app_version\":\"%s\"}}",
             firm_version, web_app_version);

    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// Handler for GET /api/v1/ota/progress endpoint
static esp_err_t ota_progress_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    char resp[64];
    sprintf(resp, "{\"progress\": %d}", ota_progress);
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

/* MQTT handlers (unchanged) */
static esp_err_t mqtt_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    nvs_handle_t nvs;
    cJSON *resp_obj = cJSON_CreateObject();
    if (nvs_open("storage", NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t enabled_u8 = 0;
        nvs_get_u8(nvs, "mqtt_en", &enabled_u8);
        cJSON_AddBoolToObject(resp_obj, "enabled", enabled_u8 ? true : false);
        char broker[101] = ""; size_t len = sizeof(broker);
        nvs_get_str(nvs, "mqtt_host", broker, &len);
        cJSON_AddStringToObject(resp_obj, "broker", broker);
        uint16_t port = 1883;
        nvs_get_u16(nvs, "mqtt_port", &port);
        cJSON_AddNumberToObject(resp_obj, "port", port);
        char user[65] = ""; len = sizeof(user);
        nvs_get_str(nvs, "mqtt_user", user, &len);
        cJSON_AddStringToObject(resp_obj, "login", user);
        char topics_str[257] = ""; len = sizeof(topics_str);
        if (nvs_get_str(nvs, "mqtt_topics", topics_str, &len) != ESP_OK) {
            strcpy(topics_str, "mqtt-get-data");
        }
        cJSON *topics_arr = cJSON_CreateArray();
        char *tok = strtok(topics_str, ",");
        while (tok) {
            while (*tok == ' ' || *tok == '\t') { tok++; }
            char *end = tok + strlen(tok) - 1;
            while (end >= tok && (*end == ' ' || *end == '\t')) {
                *end = '\0'; end--;
            }
            if (*tok != '\0') {
                cJSON_AddItemToArray(topics_arr, cJSON_CreateString(tok));
            }
            tok = strtok(NULL, ",");
        }
        cJSON_AddItemToObject(resp_obj, "topics", topics_arr);
        nvs_close(nvs);
    } else {
        cJSON_Delete(resp_obj);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read MQTT config");
        return ESP_FAIL;
    }
    char *resp_str = cJSON_PrintUnformatted(resp_obj);
    httpd_resp_sendstr(req, resp_str);
    free(resp_str);
    cJSON_Delete(resp_obj);
    return ESP_OK;
}

/* Handler for POST /api/v1/ota/firmware */
static esp_err_t ota_firmware_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    int received = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;

    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }

    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[cur_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    cJSON *url_item = cJSON_GetObjectItem(root, "url");
    if (!url_item || !cJSON_IsString(url_item)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'url'");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    esp_err_t ret = ota_start(url_item->valuestring, OTA_TYPE_FIRMWARE);
    cJSON_Delete(root);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start firmware OTA update");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "Firmware OTA update initiated");
    return ESP_OK;
}

/* Handler for POST /api/v1/ota/web_app */
static esp_err_t ota_web_app_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    int received = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;

    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }

    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[cur_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    cJSON *url_item = cJSON_GetObjectItem(root, "url");
    if (!url_item || !cJSON_IsString(url_item)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'url'");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    esp_err_t ret = ota_start(url_item->valuestring, OTA_TYPE_WEB_APP);
    cJSON_Delete(root);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start web app OTA update");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "Web App OTA update initiated");
    return ESP_OK;
}

/* Handler for POST /api/v1/ota/both */
static esp_err_t ota_both_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    int received = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;

    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[cur_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    cJSON *fw_url_item = cJSON_GetObjectItem(root, "firmware_url");
    cJSON *web_url_item = cJSON_GetObjectItem(root, "web_app_url");
    if (!fw_url_item || !cJSON_IsString(fw_url_item) ||
        !web_url_item || !cJSON_IsString(web_url_item)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'firmware_url' or 'web_app_url'");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    esp_err_t ret = ota_start_both(fw_url_item->valuestring, web_url_item->valuestring);
    cJSON_Delete(root);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start combined OTA update");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "Combined OTA update initiated");
    return ESP_OK;
}

/* Handler for POST /api/v1/mqtt */
static esp_err_t mqtt_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0, cur_len = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[cur_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READWRITE, &nvs) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open NVS");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    uint8_t new_enabled = 0;
    nvs_get_u8(nvs, "mqtt_en", &new_enabled);
    char new_broker[101] = ""; size_t len = sizeof(new_broker);
    nvs_get_str(nvs, "mqtt_host", new_broker, &len);
    uint16_t new_port = 1883;
    nvs_get_u16(nvs, "mqtt_port", &new_port);
    char new_user[65] = ""; len = sizeof(new_user);
    nvs_get_str(nvs, "mqtt_user", new_user, &len);
    char new_pass[65] = ""; len = sizeof(new_pass);
    nvs_get_str(nvs, "mqtt_pass", new_pass, &len);
    
//    char new_topics[257] = ""; len = sizeof(new_topics);
//    if (nvs_get_str(nvs, "mqtt_topics", new_topics, &len) != ESP_OK) {
//        strcpy(new_topics, "mqtt-get-data");
//    }

    cJSON *item;
    item = cJSON_GetObjectItem(root, "enabled");
    if (item) {
        if (cJSON_IsBool(item)) {
            new_enabled = cJSON_IsTrue(item) ? 1 : 0;
        } else if (cJSON_IsNumber(item)) {
            new_enabled = (item->valueint != 0) ? 1 : 0;
        } else {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid 'enabled' type");
            cJSON_Delete(root); nvs_close(nvs);
            return ESP_FAIL;
        }
    }
    item = cJSON_GetObjectItem(root, "host");
    if (item) {
        if (!cJSON_IsString(item) || item->valuestring == NULL) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid 'host' value");
            cJSON_Delete(root); nvs_close(nvs);
            return ESP_FAIL;
        }
        if (strlen(item->valuestring) >= sizeof(new_broker)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Broker address too long");
            cJSON_Delete(root); nvs_close(nvs);
            return ESP_FAIL;
        }
        strcpy(new_broker, item->valuestring);
    }
    item = cJSON_GetObjectItem(root, "port");
    if (item) {
        if (!cJSON_IsNumber(item)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid 'port' value");
            cJSON_Delete(root); nvs_close(nvs);
            return ESP_FAIL;
        }
        int port_val = item->valueint;
        if (port_val < 0 || port_val > 65535) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Port out of range");
            cJSON_Delete(root); nvs_close(nvs);
            return ESP_FAIL;
        }
        new_port = (uint16_t)port_val;
    }
    item = cJSON_GetObjectItem(root, "login");
    if (item) {
        if (!cJSON_IsString(item) || item->valuestring == NULL) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid 'login' value");
            cJSON_Delete(root); nvs_close(nvs);
            return ESP_FAIL;
        }
        if (strlen(item->valuestring) >= sizeof(new_user)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Login too long");
            cJSON_Delete(root); nvs_close(nvs);
            return ESP_FAIL;
        }
        strcpy(new_user, item->valuestring);
    }
    item = cJSON_GetObjectItem(root, "password");
    if (item) {
        if (!cJSON_IsString(item) || item->valuestring == NULL) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid 'password' value");
            cJSON_Delete(root); nvs_close(nvs);
            return ESP_FAIL;
        }
        if (strlen(item->valuestring) >= sizeof(new_pass)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password too long");
            cJSON_Delete(root); nvs_close(nvs);
            return ESP_FAIL;
        }
        strcpy(new_pass, item->valuestring);
    }

    esp_err_t err_nvs = ESP_OK;
    err_nvs |= nvs_set_u8(nvs, "mqtt_en", new_enabled);
    err_nvs |= nvs_set_str(nvs, "mqtt_host", new_broker);
    err_nvs |= nvs_set_u16(nvs, "mqtt_port", new_port);
    err_nvs |= nvs_set_str(nvs, "mqtt_user", new_user);
    err_nvs |= nvs_set_str(nvs, "mqtt_pass", new_pass);
    if (err_nvs != ESP_OK || nvs_commit(nvs) != ESP_OK) {
        nvs_close(nvs);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save MQTT config");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    nvs_close(nvs);
    cJSON_Delete(root);

    httpd_resp_sendstr(req, "MQTT settings updated");
    if (mqtt_task_handle != NULL) {
        xTaskNotifyGive(mqtt_task_handle);
    }
    return ESP_OK;
}

/**
 * Handler for POST /api/v1/led
 * Expects a JSON object: { "status": true/false }
 * Updates global status.led accordingly.
 */
static esp_err_t led_post_handler(httpd_req_t *req)
{
    // Read Content-Length
    int total_len = req->content_len;
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }

    // Read the request body into scratch buffer
    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
    char *buf = rest_context->scratch;
    int received = 0, cur_len = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[cur_len] = '\0';

    // Parse JSON payload
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Get the "status" field
    cJSON *status_item = cJSON_GetObjectItem(root, "status");
    if (!status_item || !(cJSON_IsBool(status_item) || cJSON_IsNumber(status_item))) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'status'");
        return ESP_FAIL;
    }

    // Update global LED status
    if (cJSON_IsBool(status_item)) {
        status.led = cJSON_IsTrue(status_item) ? 1 : 0;
    } else {
        status.led = (status_item->valueint != 0) ? 1 : 0;
    }
    
    LED_set_color(RED, 1);

    // Optionally notify other tasks that LED state changed
    // xSemaphoreGive(xNewDataSemaphore);

    cJSON_Delete(root);

    // Send confirmation response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"result\": \"LED status updated\"}");
    return ESP_OK;
}

/* Handler for GET /api/v1/display */
static esp_err_t display_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    char resp[64];
    sprintf(resp, "{\"display\": %u}", status.display_number);
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

/**
 * Handler for GET /api/v1/led
 * Returns the current LED status as JSON: { "status": true/false }
 */
static esp_err_t led_get_handler(httpd_req_t *req)
{
    // Set response content type to JSON
    httpd_resp_set_type(req, "application/json");

    // Prepare JSON payload with current status.led
    char resp[32];
    // status.led is an integer (0/1), convert to "true"/"false"
    snprintf(resp, sizeof(resp),
             "{\"status\": %s}",
             status.led ? "true" : "false");

    // Send response and close the connection
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// -----------------------------------------------------------------------------
// 	  Handler for GET /api/v1/pattern
//    Returns a JSON array [status.current_pattern[0], status.current_pattern[1], …]
//    No WebSocket, no semaphore — just a one‐shot HTTP response.
// -----------------------------------------------------------------------------

static esp_err_t pattern_get_handler(httpd_req_t *req)
{
    // Set response type to JSON
    httpd_resp_set_type(req, "application/json");

    // Create a cJSON array and fill it with current_pattern values
    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        // If allocation failed, send 500
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON array");
        return ESP_FAIL;
    }

    for (int i = 0; i < status.display_number; ++i) {
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(status.current_pattern[i]));
    }

    // Print JSON array as a compact string
    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to serialize JSON");
        return ESP_FAIL;
    }

    // Send JSON string and close connection
    httpd_resp_sendstr(req, json);
    cJSON_free(json);

    return ESP_OK;
}


/* Handler for GET /api/v1/mode */
static esp_err_t mode_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    wifi_mode_t wifi_mode;
    esp_wifi_get_mode(&wifi_mode);
    const char *mode_str = (wifi_mode == WIFI_MODE_AP) ? "AP" : "STA";
    char ssid[33] = "";
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READONLY, &nvs) == ESP_OK) {
        size_t ssid_len = sizeof(ssid);
        if (nvs_get_str(nvs, "ssid", ssid, &ssid_len) != ESP_OK) {
            ssid[0] = '\0';
        }
        nvs_close(nvs);
    }
    cJSON *resp_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_obj, "mode", mode_str);
    cJSON_AddStringToObject(resp_obj, "ssid", ssid);
    char *resp_str = cJSON_PrintUnformatted(resp_obj);
    httpd_resp_sendstr(req, resp_str);
    free(resp_str);
    cJSON_Delete(resp_obj);
    return ESP_OK;
}

/* Handler for POST /api/v1/mode */
static esp_err_t mode_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[cur_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    cJSON *mode_item = cJSON_GetObjectItem(root, "mode");
    if (!mode_item || !cJSON_IsString(mode_item)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'mode'");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    const char *mode_str = mode_item->valuestring;
    uint8_t new_mode;
    if (strcasecmp(mode_str, "AP") == 0) {
        new_mode = MODE_AP;
    } else if (strcasecmp(mode_str, "STA") == 0) {
        new_mode = MODE_STA;
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid mode value");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    char new_ssid[33] = {0};
    char new_pass[65] = {0};
    if (new_mode == MODE_STA) {
        cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
        cJSON *pass_item = cJSON_GetObjectItem(root, "password");
        if (!ssid_item || !cJSON_IsString(ssid_item) || strlen(ssid_item->valuestring) == 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'ssid'");
            cJSON_Delete(root);
            return ESP_FAIL;
        }
        strncpy(new_ssid, ssid_item->valuestring, sizeof(new_ssid) - 1);
        if (pass_item && cJSON_IsString(pass_item)) {
            strncpy(new_pass, pass_item->valuestring, sizeof(new_pass) - 1);
        }
        if (strlen(new_ssid) > 32 || strlen(new_pass) > 64) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID or password too long");
            cJSON_Delete(root);
            return ESP_FAIL;
        }
    }
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open NVS");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    err = nvs_set_u8(nvs_handle, "mode", new_mode);
    if (err == ESP_OK && new_mode == MODE_STA) {
        err = nvs_set_str(nvs_handle, "ssid", new_ssid);
        if (err == ESP_OK) {
            err = nvs_set_str(nvs_handle, "password", new_pass);
        }
    }
    if (err != ESP_OK || nvs_commit(nvs_handle) != ESP_OK) {
        nvs_close(nvs_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    nvs_close(nvs_handle);
    cJSON_Delete(root);
    httpd_resp_sendstr(req, "Mode updated, rebooting...");
    vTaskDelay(250/ portTICK_PERIOD_MS);
    esp_restart();
    return ESP_OK;
}

// Handler for POST /api/v1/clock
// Expects a JSON array: [second, minute, hour, day, month, year]
static esp_err_t clock_post_handler(httpd_req_t *req)
{
    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
    int total_len = req->content_len;

    // Reject if payload is too large for our scratch buffer
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }

    // Read the entire request body into our buffer
    char *buf = rest_context->scratch;
    int received = httpd_req_recv(req, buf, total_len);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    // Parse the JSON payload
    cJSON *root = cJSON_Parse(buf);
    if (!root || !cJSON_IsArray(root) || cJSON_GetArraySize(root) != 6) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected JSON array of 6 numbers");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Assign each element of the array to the corresponding RTC field
    cJSON *item;
    item = cJSON_GetArrayItem(root, 0);
    if (cJSON_IsNumber(item)) status.rtc.second = (uint8_t)item->valueint;
    item = cJSON_GetArrayItem(root, 1);
    if (cJSON_IsNumber(item)) status.rtc.minute = (uint8_t)item->valueint;
    item = cJSON_GetArrayItem(root, 2);
    if (cJSON_IsNumber(item)) status.rtc.hour   = (uint8_t)item->valueint;
    item = cJSON_GetArrayItem(root, 3);
    if (cJSON_IsNumber(item)) status.rtc.day    = (uint8_t)item->valueint;
    item = cJSON_GetArrayItem(root, 4);
    if (cJSON_IsNumber(item)) status.rtc.month  = (uint8_t)item->valueint;
    item = cJSON_GetArrayItem(root, 5);
    if (cJSON_IsNumber(item)) status.rtc.year   = (uint8_t)item->valueint;

	//Update RTC
	uint8_t current_time[7];
	
	current_time[0] = status.rtc.second;
	current_time[1] = status.rtc.minute;
	current_time[2] = status.rtc.hour;
	current_time[3] = 1;
	current_time[4] = status.rtc.day;
	current_time[5] = status.rtc.month;
	current_time[6] = status.rtc.year;
	
	//ESP_LOGI(REST_TAG, "RAW CLOCK %d:%d:%d %d:%d:%d", status.rtc.hour, status.rtc.minute, status.rtc.second, status.rtc.day, status.rtc.month, status.rtc.year);
	
	// Clean up JSON object
    cJSON_Delete(root);

	ds3231_set(TIME, current_time);

    // Notify other tasks that new RTC data is available (if you use a semaphore)
    //xSemaphoreGive(xNewDataSemaphore);

    // Send response and close the connection
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, "RTC updated successfully");
    return ESP_OK;
}

esp_err_t start_rest_server(const char *base_path)
{
    REST_CHECK(base_path, "wrong base path", err);
    rest_server_context_t *rest_context = calloc(1, sizeof(rest_server_context_t));
    REST_CHECK(rest_context, "No memory for rest context", err);
    strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 20;

    ESP_LOGI(REST_TAG, "Starting HTTP Server");
    REST_CHECK(httpd_start(&server, &config) == ESP_OK, "Start server failed", err_start);
    
    httpd_uri_t config_get_uri = {
	    .uri      = "/api/v1/config",
	    .method   = HTTP_GET,
	    .handler  = config_get_handler,
	    .user_ctx = rest_context
	};
	httpd_register_uri_handler(server, &config_get_uri);
	
	httpd_uri_t config_post_uri = {
	    .uri      = "/api/v1/config",
	    .method   = HTTP_POST,
	    .handler  = config_post_handler,
	    .user_ctx = rest_context
	};
	httpd_register_uri_handler(server, &config_post_uri);
	    
    httpd_uri_t versions_get_uri = {
        .uri      = "/api/v1/versions",
        .method   = HTTP_GET,
        .handler  = versions_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &versions_get_uri);
    
    httpd_uri_t ota_progress_get_uri = {
        .uri      = "/api/v1/ota/progress",
        .method   = HTTP_GET,
        .handler  = ota_progress_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &ota_progress_get_uri);

    httpd_uri_t mqtt_get_uri = {
        .uri      = "/api/v1/mqtt",
        .method   = HTTP_GET,
        .handler  = mqtt_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &mqtt_get_uri);

    httpd_uri_t mqtt_post_uri = {
        .uri      = "/api/v1/mqtt",
        .method   = HTTP_POST,
        .handler  = mqtt_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &mqtt_post_uri);
    
    httpd_uri_t display_get_uri = {
        .uri      = "/api/v1/display",
        .method   = HTTP_GET,
        .handler  = display_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &display_get_uri);
    
	httpd_uri_t pattern_get_uri = {
	    .uri      = "/api/v1/pattern",      
	    .method   = HTTP_GET,               
	    .handler  = pattern_get_handler,    
	    .user_ctx = rest_context
	};
	httpd_register_uri_handler(server, &pattern_get_uri);
    

    httpd_uri_t mode_get_uri = {
        .uri      = "/api/v1/mode",
        .method   = HTTP_GET,
        .handler  = mode_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &mode_get_uri);

    httpd_uri_t mode_post_uri = {
        .uri      = "/api/v1/mode",
        .method   = HTTP_POST,
        .handler  = mode_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &mode_post_uri);
  
    httpd_uri_t ota_firmware_post_uri = {
        .uri      = "/api/v1/ota/firmware",
        .method   = HTTP_POST,
        .handler  = ota_firmware_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &ota_firmware_post_uri);
    
    httpd_uri_t ota_web_app_post_uri = {
        .uri      = "/api/v1/ota/web_app",
        .method   = HTTP_POST,
        .handler  = ota_web_app_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &ota_web_app_post_uri);

    httpd_uri_t ota_both_post_uri = {
        .uri      = "/api/v1/ota/both",
        .method   = HTTP_POST,
        .handler  = ota_both_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &ota_both_post_uri);
    
    // In start_rest_server(), register the new /api/v1/clock POST endpoint:
	httpd_uri_t clock_post_uri = {
	    .uri      = "/api/v1/clock",
	    .method   = HTTP_POST,
	    .handler  = clock_post_handler,
	    .user_ctx = rest_context
	};
	httpd_register_uri_handler(server, &clock_post_uri);
	
	httpd_uri_t led_get_uri = {
        .uri      = "/api/v1/led",
        .method   = HTTP_GET,
        .handler  = led_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &led_get_uri);

    // Register POST /api/v1/led
    httpd_uri_t led_post_uri = {
        .uri      = "/api/v1/led",
        .method   = HTTP_POST,
        .handler  = led_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &led_post_uri);	
    
    httpd_uri_t common_get_uri = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = rest_common_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &common_get_uri);

    return ESP_OK;
err_start:
    free(rest_context);
err:
    return ESP_FAIL;
}

