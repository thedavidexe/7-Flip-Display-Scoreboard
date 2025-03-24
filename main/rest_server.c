/* HTTP Restful API Server

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
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

extern status_t status;  // Global device status (from main.c)

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
        // Respond with 500 Internal Server Error if file not found
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }
    set_content_type_from_file(req, filepath);

    char *chunk = rest_context->scratch;
    ssize_t read_bytes;
    do {
        // Read file in chunks into the scratch buffer
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(REST_TAG, "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            // Send the buffer contents as HTTP response chunk
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(REST_TAG, "File sending failed!");
                httpd_resp_sendstr_chunk(req, NULL);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    // Close file after sending complete
    close(fd);
    ESP_LOGI(REST_TAG, "File sending complete");
    // Respond with an empty chunk to signal HTTP response completion
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* MQTT */
/* Handler for GET /api/v1/mqtt (returns current MQTT configuration) */
static esp_err_t mqtt_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    // Open NVS to read stored MQTT settings
    nvs_handle_t nvs;
    cJSON *resp_obj = cJSON_CreateObject();
    if (nvs_open("storage", NVS_READONLY, &nvs) == ESP_OK) {
        // Read enabled flag (default false if missing)
        uint8_t enabled_u8 = 0;
        nvs_get_u8(nvs, "mqtt_en", &enabled_u8);
        cJSON_AddBoolToObject(resp_obj, "enabled", enabled_u8 ? true : false);
        // Read broker address (default empty if missing)
        char broker[101] = ""; size_t len = sizeof(broker);
        nvs_get_str(nvs, "mqtt_host", broker, &len);
        cJSON_AddStringToObject(resp_obj, "broker", broker);
        // Read port (default 1883 if missing)
        uint16_t port = 1883;
        nvs_get_u16(nvs, "mqtt_port", &port);
        cJSON_AddNumberToObject(resp_obj, "port", port);
        // Read username (login) (default empty if missing)
        char user[65] = ""; len = sizeof(user);
        nvs_get_str(nvs, "mqtt_user", user, &len);
        cJSON_AddStringToObject(resp_obj, "login", user);
        // (Note: Password is not returned for security reasons)
        // Read topics list and split into JSON array
        char topics_str[257] = ""; len = sizeof(topics_str);
        if (nvs_get_str(nvs, "mqtt_topics", topics_str, &len) != ESP_OK) {
            // If not found, use default topic
            strcpy(topics_str, "mqtt-get-data");
        }
        cJSON *topics_arr = cJSON_CreateArray();
        char *tok = strtok(topics_str, ",");
        while (tok) {
            // Trim whitespace around each topic
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
        // Failed to open NVS
        cJSON_Delete(resp_obj);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read MQTT config");
        return ESP_FAIL;
    }
    // Send JSON response
    char *resp_str = cJSON_PrintUnformatted(resp_obj);
    httpd_resp_sendstr(req, resp_str);
    free(resp_str);
    cJSON_Delete(resp_obj);
    return ESP_OK;
}

/* Handler for POST /api/v1/ota */
static esp_err_t ota_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    int received = 0;
    // Use the scratch buffer from the rest_server_context
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;

    // Check if content length exceeds buffer size
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }

    // Receive the POST data in chunks
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[cur_len] = '\0';

    // Parse the JSON content
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Extract the "url" field from the JSON
    cJSON *url_item = cJSON_GetObjectItem(root, "url");
    if (url_item == NULL || !cJSON_IsString(url_item) || (url_item->valuestring == NULL)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'url'");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Call the OTA start function with the provided URL
    esp_err_t ret = ota_start(url_item->valuestring);
    cJSON_Delete(root);

    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start OTA update");
        return ESP_FAIL;
    }

    // Respond with a success message
    httpd_resp_sendstr(req, "OTA update initiated");
    return ESP_OK;
}

/* Handler for POST /api/v1/mqtt (updates MQTT configuration) */
static esp_err_t mqtt_post_handler(httpd_req_t *req)
{
    // Receive the JSON request body
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

    // Parse JSON content
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Prepare to load existing config (to allow partial updates)
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READWRITE, &nvs) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open NVS");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    // Load current values as defaults
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
    char new_topics[257] = ""; len = sizeof(new_topics);
    if (nvs_get_str(nvs, "mqtt_topics", new_topics, &len) != ESP_OK) {
        strcpy(new_topics, "mqtt-get-data");
    }

    // Update with values from request JSON (if present)
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
    if (item) {  // broker address (host)
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
    if (item) {  // username
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
    item = cJSON_GetObjectItem(root, "topics");
    if (item) {
        if (cJSON_IsArray(item)) {
            // Build comma-separated topics string from array
            new_topics[0] = '\0';
            bool first = true;
            cJSON *topic_elem;
            cJSON_ArrayForEach(topic_elem, item) {
                if (!cJSON_IsString(topic_elem) || topic_elem->valuestring == NULL) {
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid topic in list");
                    cJSON_Delete(root); nvs_close(nvs);
                    return ESP_FAIL;
                }
                // Skip empty strings
                if (strlen(topic_elem->valuestring) == 0) {
                    continue;
                }
                if (!first) {
                    // Add comma separator if not the first valid topic
                    if (strlen(new_topics) + 1 < sizeof(new_topics)) {
                        strcat(new_topics, ",");
                    }
                }
                first = false;
                // Append topic
                if (strlen(new_topics) + strlen(topic_elem->valuestring) >= sizeof(new_topics)) {
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Topics list too long");
                    cJSON_Delete(root); nvs_close(nvs);
                    return ESP_FAIL;
                }
                strcat(new_topics, topic_elem->valuestring);
            }
        } else if (cJSON_IsString(item) && item->valuestring != NULL) {
            // If topics provided as a single comma-separated string
            if (strlen(item->valuestring) >= sizeof(new_topics)) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Topics string too long");
                cJSON_Delete(root); nvs_close(nvs);
                return ESP_FAIL;
            }
            strcpy(new_topics, item->valuestring);
        } else {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid 'topics' value");
            cJSON_Delete(root); nvs_close(nvs);
            return ESP_FAIL;
        }
    }

    // Save updated settings to NVS
    esp_err_t err = ESP_OK;
    err |= nvs_set_u8(nvs, "mqtt_en", new_enabled);
    err |= nvs_set_str(nvs, "mqtt_host", new_broker);
    err |= nvs_set_u16(nvs, "mqtt_port", new_port);
    err |= nvs_set_str(nvs, "mqtt_user", new_user);
    err |= nvs_set_str(nvs, "mqtt_pass", new_pass);
    err |= nvs_set_str(nvs, "mqtt_topics", new_topics);
    if (err != ESP_OK || nvs_commit(nvs) != ESP_OK) {
        nvs_close(nvs);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save MQTT config");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    nvs_close(nvs);
    cJSON_Delete(root);

    // Respond with success message
    httpd_resp_sendstr(req, "MQTT settings updated");
    // Notify the MQTT task to apply new configuration (no reboot needed)
    if (mqtt_task_handle != NULL) {
        xTaskNotifyGive(mqtt_task_handle);
    }
    return ESP_OK;
}

/* Handler for POST /api/v1/score/my_score (example endpoint) */
static esp_err_t score_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }
    // Receive the POST data in chunks
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
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    cJSON *score_item = cJSON_GetObjectItem(root, "score");
    if (score_item == NULL || !cJSON_IsNumber(score_item)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'score'");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    int score = score_item->valueint;
    ESP_LOGI(REST_TAG, "Received score: %d", score);
    DisplayNumber(score / 10);  // Update display with received score (example action)
    cJSON_Delete(root);
    httpd_resp_sendstr(req, "Score received successfully");
    return ESP_OK;
}

/* Handler for GET /api/v1/display (returns current display value) */
static esp_err_t display_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    char resp[64];
    // Convert the display value to JSON format
    sprintf(resp, "{\"display\": %u}", status.display_number);
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

/* Handler for GET /api/v1/mode (returns current Wi-Fi mode and SSID) */
static esp_err_t mode_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    // Determine current Wi-Fi mode
    wifi_mode_t wifi_mode;
    esp_wifi_get_mode(&wifi_mode);
    const char *mode_str = (wifi_mode == WIFI_MODE_AP) ? "AP" : "STA";
    // Retrieve stored station SSID from NVS
    char ssid[33] = "";
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READONLY, &nvs) == ESP_OK) {
        size_t ssid_len = sizeof(ssid);
        if (nvs_get_str(nvs, "ssid", ssid, &ssid_len) != ESP_OK) {
            ssid[0] = '\0';
        }
        nvs_close(nvs);
    }
    // Build JSON response
    cJSON *resp_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_obj, "mode", mode_str);
    cJSON_AddStringToObject(resp_obj, "ssid", ssid);
    char *resp_str = cJSON_PrintUnformatted(resp_obj);
    httpd_resp_sendstr(req, resp_str);
    free(resp_str);
    cJSON_Delete(resp_obj);
    return ESP_OK;
}

/* Handler for POST /api/v1/mode (sets new Wi-Fi mode and credentials) */
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
    // Receive the POST data in chunks
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[cur_len] = '\0';

    // Parse JSON body
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
        // Copy SSID and password (password may be empty for open network)
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
    // Save new settings in NVS
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
    
    // Add delay to give the server time to respond before it resets
    vTaskDelay(250/ portTICK_PERIOD_MS);
    // Restart device to apply new Wi-Fi settings
    esp_restart();
    return ESP_OK;  // (Device will restart immediately after sending response)
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

    ESP_LOGI(REST_TAG, "Starting HTTP Server");
    REST_CHECK(httpd_start(&server, &config) == ESP_OK, "Start server failed", err_start);

    // Register REST API endpoints
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
    
    httpd_uri_t score_post_uri = {
        .uri      = "/api/v1/score/my_score",
        .method   = HTTP_POST,
        .handler  = score_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &score_post_uri);

    httpd_uri_t display_get_uri = {
        .uri      = "/api/v1/display",
        .method   = HTTP_GET,
        .handler  = display_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &display_get_uri);

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

    // Catch-all handler for static web content
    httpd_uri_t common_get_uri = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = rest_common_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &common_get_uri);
    
    httpd_uri_t ota_post_uri = {
	    .uri      = "/api/v1/ota",
	    .method   = HTTP_POST,
	    .handler  = ota_post_handler,
	    .user_ctx = rest_context
	};
	httpd_register_uri_handler(server, &ota_post_uri);

    return ESP_OK;
err_start:
    free(rest_context);
err:
    return ESP_FAIL;
}