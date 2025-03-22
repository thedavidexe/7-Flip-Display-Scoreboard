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

    return ESP_OK;
err_start:
    free(rest_context);
err:
    return ESP_FAIL;
}