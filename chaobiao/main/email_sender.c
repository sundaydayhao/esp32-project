#include "email_sender.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "sdkconfig.h"

static const char *TAG = "email_sender";

#define SMTP_COMMAND_BUFFER_SIZE  768
#define SMTP_LINE_BUFFER_SIZE     256
#define SMTP_IO_BUFFER_SIZE       128
#define SMTP_MIME_BOUNDARY        "----esp32cam-gas-meter-boundary-98a8"
#define SMTP_CLIENT_NAME          "esp32-cam"
#define SMTP_IMAGE_SUBJECT        "ESP32-CAM gas meter capture"
#define SMTP_IMAGE_BODY           "Attached is the latest gas meter capture from the ESP32-CAM device."
#define SMTP_TEXT_SUBJECT         "ESP32-CAM gas meter notification"
#define SMTP_ATTACHMENT_NAME      "gas-meter.jpg"
#define SMTP_ATTACHMENT_MIME_TYPE "image/jpeg"

typedef struct {
    mbedtls_net_context socket;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
    bool tls_active;
    bool socket_connected;
} smtp_connection_t;

static bool string_has_line_breaks(const char *value)
{
    return value != NULL && (strchr(value, '\r') != NULL || strchr(value, '\n') != NULL);
}

static esp_err_t validate_email_config(void)
{
    bool missing = false;

    if (CONFIG_SMTP_SERVER[0] == '\0') {
        ESP_LOGE(TAG, "SMTP server is empty. Set CONFIG_SMTP_SERVER in menuconfig.");
        missing = true;
    }
    if (CONFIG_SMTP_PORT[0] == '\0') {
        ESP_LOGE(TAG, "SMTP port is empty. Set CONFIG_SMTP_PORT in menuconfig.");
        missing = true;
    }
    if (CONFIG_SMTP_SENDER_EMAIL[0] == '\0') {
        ESP_LOGE(TAG, "SMTP sender email is empty. Set CONFIG_SMTP_SENDER_EMAIL in menuconfig.");
        missing = true;
    }
    if (CONFIG_SMTP_SENDER_AUTH_CODE[0] == '\0') {
        ESP_LOGE(TAG, "SMTP sender auth code is empty. For 163 mail, fill the mailbox SMTP auth code, not the login password.");
        missing = true;
    }
    if (CONFIG_SMTP_RECIPIENT_EMAIL[0] == '\0') {
        ESP_LOGE(TAG, "SMTP recipient email is empty. Set CONFIG_SMTP_RECIPIENT_EMAIL in menuconfig.");
        missing = true;
    }

    if (missing) {
        return ESP_ERR_INVALID_STATE;
    }

    if (string_has_line_breaks(CONFIG_SMTP_SERVER) ||
        string_has_line_breaks(CONFIG_SMTP_PORT) ||
        string_has_line_breaks(CONFIG_SMTP_SENDER_EMAIL) ||
        string_has_line_breaks(CONFIG_SMTP_SENDER_AUTH_CODE) ||
        string_has_line_breaks(CONFIG_SMTP_RECIPIENT_EMAIL)) {
        ESP_LOGE(TAG, "SMTP config contains unsupported line breaks.");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static void smtp_connection_init(smtp_connection_t *conn)
{
    memset(conn, 0, sizeof(*conn));
    mbedtls_net_init(&conn->socket);
    mbedtls_ssl_init(&conn->ssl);
    mbedtls_ssl_config_init(&conn->config);
    mbedtls_ctr_drbg_init(&conn->ctr_drbg);
    mbedtls_entropy_init(&conn->entropy);
}

static void smtp_connection_cleanup(smtp_connection_t *conn)
{
    if (conn == NULL) {
        return;
    }

    if (conn->tls_active) {
        mbedtls_ssl_close_notify(&conn->ssl);
    }

    if (conn->socket_connected) {
        mbedtls_net_free(&conn->socket);
    }

    mbedtls_ssl_free(&conn->ssl);
    mbedtls_ssl_config_free(&conn->config);
    mbedtls_ctr_drbg_free(&conn->ctr_drbg);
    mbedtls_entropy_free(&conn->entropy);
}

static void log_mbedtls_error(const char *step, int ret)
{
    if (ret == 0) {
        ESP_LOGE(TAG, "%s failed: connection closed", step);
        return;
    }

    char error_buf[128];

    mbedtls_strerror(ret, error_buf, sizeof(error_buf));
    ESP_LOGE(TAG, "%s failed: -0x%x (%s)", step, -ret, error_buf);
}

static esp_err_t smtp_send_raw(smtp_connection_t *conn, const unsigned char *data, size_t len)
{
    size_t offset = 0;

    while (offset < len) {
        int ret = 0;
        if (conn->tls_active) {
            ret = mbedtls_ssl_write(&conn->ssl, data + offset, len - offset);
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
        } else {
            ret = mbedtls_net_send(&conn->socket, data + offset, len - offset);
        }

        if (ret <= 0) {
            if (conn->tls_active) {
                log_mbedtls_error("SMTP write", ret);
            } else {
                ESP_LOGE(TAG, "SMTP write failed: %d", ret);
            }
            return ESP_FAIL;
        }

        offset += (size_t)ret;
    }

    return ESP_OK;
}

static int smtp_receive_raw(smtp_connection_t *conn, unsigned char *buffer, size_t len)
{
    while (1) {
        int ret = conn->tls_active
                      ? mbedtls_ssl_read(&conn->ssl, buffer, len)
                      : mbedtls_net_recv(&conn->socket, buffer, len);

        if (conn->tls_active &&
            (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)) {
            continue;
        }

        return ret;
    }
}

static esp_err_t smtp_expect_response(smtp_connection_t *conn, int min_code, int max_code)
{
    unsigned char data[SMTP_IO_BUFFER_SIZE];
    char line[SMTP_LINE_BUFFER_SIZE];
    size_t line_len = 0;

    while (1) {
        int ret = smtp_receive_raw(conn, data, sizeof(data));
        if (ret <= 0) {
            if (conn->tls_active) {
                log_mbedtls_error("SMTP read", ret);
            } else {
                ESP_LOGE(TAG, "SMTP read failed: %d", ret);
            }
            return ESP_FAIL;
        }

        for (int i = 0; i < ret; i++) {
            char c = (char)data[i];

            if (c == '\r') {
                continue;
            }

            if (c == '\n') {
                line[line_len] = '\0';
                if (line_len > 0) {
                    ESP_LOGD(TAG, "SMTP <= %s", line);
                }

                if (line_len >= 4 &&
                    isdigit((unsigned char)line[0]) &&
                    isdigit((unsigned char)line[1]) &&
                    isdigit((unsigned char)line[2]) &&
                    line[3] == ' ') {
                    int code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
                    if (code < min_code || code > max_code) {
                        ESP_LOGE(TAG, "Unexpected SMTP response code: %d", code);
                        return ESP_FAIL;
                    }

                    return ESP_OK;
                }

                line_len = 0;
                continue;
            }

            if (line_len < sizeof(line) - 1) {
                line[line_len++] = c;
            }
        }
    }
}

static esp_err_t smtp_send_command(smtp_connection_t *conn,
                                   const char *command,
                                   int min_code,
                                   int max_code,
                                   const char *log_label)
{
    if (log_label != NULL) {
        ESP_LOGI(TAG, "%s", log_label);
    }

    esp_err_t ret = smtp_send_raw(conn, (const unsigned char *)command, strlen(command));
    if (ret != ESP_OK) {
        return ret;
    }

    return smtp_expect_response(conn, min_code, max_code);
}

static esp_err_t smtp_handshake(smtp_connection_t *conn)
{
    int ret = 0;

    ESP_LOGI(TAG, "Performing TLS handshake with %s", CONFIG_SMTP_SERVER);
    while ((ret = mbedtls_ssl_handshake(&conn->ssl)) != 0) {
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }

        log_mbedtls_error("TLS handshake", ret);
        return ESP_FAIL;
    }

    uint32_t verify_flags = mbedtls_ssl_get_verify_result(&conn->ssl);
    if (verify_flags != 0) {
        char verify_buf[192];

        mbedtls_x509_crt_verify_info(verify_buf, sizeof(verify_buf), "  ", verify_flags);
        ESP_LOGE(TAG, "SMTP server certificate verification failed: %s", verify_buf);
        return ESP_FAIL;
    }

    conn->tls_active = true;
    ESP_LOGI(TAG, "TLS established, cipher suite: %s", mbedtls_ssl_get_ciphersuite(&conn->ssl));
    return ESP_OK;
}

static esp_err_t smtp_open_connection(smtp_connection_t *conn)
{
    static const char *seed = "esp32cam_smtp";

    smtp_connection_init(conn);

    int ret = mbedtls_ctr_drbg_seed(&conn->ctr_drbg,
                                    mbedtls_entropy_func,
                                    &conn->entropy,
                                    (const unsigned char *)seed,
                                    strlen(seed));
    if (ret != 0) {
        log_mbedtls_error("mbedtls_ctr_drbg_seed", ret);
        return ESP_FAIL;
    }

    ret = mbedtls_ssl_config_defaults(&conn->config,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        log_mbedtls_error("mbedtls_ssl_config_defaults", ret);
        return ESP_FAIL;
    }

    mbedtls_ssl_conf_authmode(&conn->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&conn->config, mbedtls_ctr_drbg_random, &conn->ctr_drbg);

    esp_err_t attach_ret = esp_crt_bundle_attach(&conn->config);
    if (attach_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach certificate bundle: %s", esp_err_to_name(attach_ret));
        return attach_ret;
    }

    ret = mbedtls_ssl_setup(&conn->ssl, &conn->config);
    if (ret != 0) {
        log_mbedtls_error("mbedtls_ssl_setup", ret);
        return ESP_FAIL;
    }

    ret = mbedtls_ssl_set_hostname(&conn->ssl, CONFIG_SMTP_SERVER);
    if (ret != 0) {
        log_mbedtls_error("mbedtls_ssl_set_hostname", ret);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connecting to %s:%s", CONFIG_SMTP_SERVER, CONFIG_SMTP_PORT);
    ret = mbedtls_net_connect(&conn->socket,
                              CONFIG_SMTP_SERVER,
                              CONFIG_SMTP_PORT,
                              MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        log_mbedtls_error("mbedtls_net_connect", ret);
        return ESP_FAIL;
    }

    conn->socket_connected = true;
    mbedtls_ssl_set_bio(&conn->ssl, &conn->socket, mbedtls_net_send, mbedtls_net_recv, NULL);

#ifdef CONFIG_SMTP_USE_STARTTLS
    return ESP_OK;
#else
    return smtp_handshake(conn);
#endif
}

static esp_err_t smtp_send_ehlo(smtp_connection_t *conn)
{
    char command[SMTP_COMMAND_BUFFER_SIZE];
    int len = snprintf(command, sizeof(command), "EHLO %s\r\n", SMTP_CLIENT_NAME);
    if (len < 0 || len >= (int)sizeof(command)) {
        ESP_LOGE(TAG, "Failed to format EHLO command");
        return ESP_FAIL;
    }

    return smtp_send_command(conn, command, 200, 299, "Sending EHLO");
}

static esp_err_t smtp_begin_session(smtp_connection_t *conn)
{
#ifdef CONFIG_SMTP_USE_STARTTLS
    esp_err_t ret = smtp_expect_response(conn, 200, 299);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = smtp_send_ehlo(conn);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = smtp_send_command(conn, "STARTTLS\r\n", 200, 299, "Starting TLS");
    if (ret != ESP_OK) {
        return ret;
    }

    ret = smtp_handshake(conn);
    if (ret != ESP_OK) {
        return ret;
    }

    return smtp_send_ehlo(conn);
#else
    esp_err_t ret = smtp_expect_response(conn, 200, 299);
    if (ret != ESP_OK) {
        return ret;
    }

    return smtp_send_ehlo(conn);
#endif
}

static esp_err_t base64_encode_alloc(const char *input, char **output)
{
    size_t input_len = strlen(input);
    size_t output_capacity = ((input_len + 2) / 3) * 4 + 1;
    unsigned char *buffer = (unsigned char *)malloc(output_capacity);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t output_len = 0;
    int ret = mbedtls_base64_encode(buffer,
                                    output_capacity,
                                    &output_len,
                                    (const unsigned char *)input,
                                    input_len);
    if (ret != 0) {
        free(buffer);
        log_mbedtls_error("mbedtls_base64_encode", ret);
        return ESP_FAIL;
    }

    buffer[output_len] = '\0';
    *output = (char *)buffer;
    return ESP_OK;
}

static esp_err_t smtp_authenticate(smtp_connection_t *conn)
{
    char *encoded_user = NULL;
    char *encoded_password = NULL;
    char command[SMTP_COMMAND_BUFFER_SIZE];

    esp_err_t ret = base64_encode_alloc(CONFIG_SMTP_SENDER_EMAIL, &encoded_user);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = base64_encode_alloc(CONFIG_SMTP_SENDER_AUTH_CODE, &encoded_password);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = smtp_send_command(conn, "AUTH LOGIN\r\n", 300, 399, "Authenticating with SMTP server");
    if (ret != ESP_OK) {
        goto cleanup;
    }

    int len = snprintf(command, sizeof(command), "%s\r\n", encoded_user);
    if (len < 0 || len >= (int)sizeof(command)) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    ret = smtp_send_command(conn, command, 300, 399, "Sending SMTP username");
    if (ret != ESP_OK) {
        goto cleanup;
    }

    len = snprintf(command, sizeof(command), "%s\r\n", encoded_password);
    if (len < 0 || len >= (int)sizeof(command)) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    ret = smtp_send_command(conn, command, 200, 399, "Sending SMTP auth code");

cleanup:
    free(encoded_user);
    free(encoded_password);
    return ret;
}

static esp_err_t smtp_write_header_block(smtp_connection_t *conn,
                                         const char *subject,
                                         const char *body,
                                         bool has_attachment)
{
    char header[SMTP_COMMAND_BUFFER_SIZE];
    int len = snprintf(header, sizeof(header),
                       "From: ESP32-CAM Gas Meter <%s>\r\n"
                       "To: <%s>\r\n"
                       "Subject: %s\r\n"
                       "MIME-Version: 1.0\r\n"
                       "Content-Type: multipart/mixed; boundary=\"%s\"\r\n"
                       "\r\n"
                       "--%s\r\n"
                       "Content-Type: text/plain; charset=UTF-8\r\n"
                       "Content-Transfer-Encoding: 7bit\r\n"
                       "\r\n",
                       CONFIG_SMTP_SENDER_EMAIL,
                       CONFIG_SMTP_RECIPIENT_EMAIL,
                       subject,
                       SMTP_MIME_BOUNDARY,
                       SMTP_MIME_BOUNDARY);
    if (len < 0 || len >= (int)sizeof(header)) {
        ESP_LOGE(TAG, "Email header is too large");
        return ESP_FAIL;
    }

    esp_err_t ret = smtp_send_raw(conn, (const unsigned char *)header, (size_t)len);
    if (ret != ESP_OK) {
        return ret;
    }

    bool line_start = true;
    char chunk[SMTP_IO_BUFFER_SIZE];
    size_t chunk_len = 0;

    for (size_t i = 0; body[i] != '\0'; i++) {
        char c = body[i];

        if (line_start && c == '.') {
            if (chunk_len == sizeof(chunk)) {
                ret = smtp_send_raw(conn, (const unsigned char *)chunk, chunk_len);
                if (ret != ESP_OK) {
                    return ret;
                }
                chunk_len = 0;
            }
            chunk[chunk_len++] = '.';
        }

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            if (chunk_len + 2 > sizeof(chunk)) {
                ret = smtp_send_raw(conn, (const unsigned char *)chunk, chunk_len);
                if (ret != ESP_OK) {
                    return ret;
                }
                chunk_len = 0;
            }

            chunk[chunk_len++] = '\r';
            chunk[chunk_len++] = '\n';
            line_start = true;
            continue;
        }

        if (chunk_len == sizeof(chunk)) {
            ret = smtp_send_raw(conn, (const unsigned char *)chunk, chunk_len);
            if (ret != ESP_OK) {
                return ret;
            }
            chunk_len = 0;
        }

        chunk[chunk_len++] = c;
        line_start = false;
    }

    if (chunk_len > 0) {
        ret = smtp_send_raw(conn, (const unsigned char *)chunk, chunk_len);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    ret = smtp_send_raw(conn, (const unsigned char *)"\r\n", 2);
    if (ret != ESP_OK || !has_attachment) {
        return ret;
    }

    len = snprintf(header, sizeof(header),
                   "\r\n--%s\r\n"
                   "Content-Type: %s; name=\"%s\"\r\n"
                   "Content-Transfer-Encoding: base64\r\n"
                   "Content-Disposition: attachment; filename=\"%s\"\r\n"
                   "\r\n",
                   SMTP_MIME_BOUNDARY,
                   SMTP_ATTACHMENT_MIME_TYPE,
                   SMTP_ATTACHMENT_NAME,
                   SMTP_ATTACHMENT_NAME);
    if (len < 0 || len >= (int)sizeof(header)) {
        ESP_LOGE(TAG, "Attachment header is too large");
        return ESP_FAIL;
    }

    return smtp_send_raw(conn, (const unsigned char *)header, (size_t)len);
}

static esp_err_t smtp_write_attachment_base64(smtp_connection_t *conn,
                                              const uint8_t *attachment_data,
                                              size_t attachment_len)
{
    unsigned char encoded[80];
    char line[84];

    for (size_t offset = 0; offset < attachment_len; ) {
        size_t chunk_len = attachment_len - offset;
        if (chunk_len > 57) {
            chunk_len = 57;
        }

        size_t encoded_len = 0;
        int ret = mbedtls_base64_encode(encoded,
                                        sizeof(encoded),
                                        &encoded_len,
                                        attachment_data + offset,
                                        chunk_len);
        if (ret != 0) {
            log_mbedtls_error("mbedtls_base64_encode", ret);
            return ESP_FAIL;
        }

        encoded[encoded_len] = '\0';
        int line_len = snprintf(line, sizeof(line), "%s\r\n", encoded);
        if (line_len < 0 || line_len >= (int)sizeof(line)) {
            return ESP_FAIL;
        }

        esp_err_t send_ret = smtp_send_raw(conn, (const unsigned char *)line, (size_t)line_len);
        if (send_ret != ESP_OK) {
            return send_ret;
        }

        offset += chunk_len;
    }

    return ESP_OK;
}

static esp_err_t smtp_send_email(const char *subject,
                                 const char *body,
                                 const uint8_t *attachment_data,
                                 size_t attachment_len)
{
    smtp_connection_t conn;
    esp_err_t ret = validate_email_config();
    if (ret != ESP_OK) {
        return ret;
    }

    if (subject == NULL || body == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bool has_attachment = attachment_data != NULL && attachment_len > 0;

    ret = smtp_open_connection(&conn);
    if (ret != ESP_OK) {
        smtp_connection_cleanup(&conn);
        return ret;
    }

    ret = smtp_begin_session(&conn);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = smtp_authenticate(&conn);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    char command[SMTP_COMMAND_BUFFER_SIZE];

    int len = snprintf(command, sizeof(command), "MAIL FROM:<%s>\r\n", CONFIG_SMTP_SENDER_EMAIL);
    if (len < 0 || len >= (int)sizeof(command)) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    ret = smtp_send_command(&conn, command, 200, 299, "Sending MAIL FROM");
    if (ret != ESP_OK) {
        goto cleanup;
    }

    len = snprintf(command, sizeof(command), "RCPT TO:<%s>\r\n", CONFIG_SMTP_RECIPIENT_EMAIL);
    if (len < 0 || len >= (int)sizeof(command)) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    ret = smtp_send_command(&conn, command, 200, 299, "Sending RCPT TO");
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = smtp_send_command(&conn, "DATA\r\n", 300, 399, "Sending DATA");
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = smtp_write_header_block(&conn, subject, body, has_attachment);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    if (has_attachment) {
        ret = smtp_write_attachment_base64(&conn, attachment_data, attachment_len);
        if (ret != ESP_OK) {
            goto cleanup;
        }
    }

    len = snprintf(command, sizeof(command), "\r\n--%s--\r\n.\r\n", SMTP_MIME_BOUNDARY);
    if (len < 0 || len >= (int)sizeof(command)) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    ret = smtp_send_command(&conn, command, 200, 299, "Finalizing email");
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ESP_LOGI(TAG, "Email sent successfully");

cleanup:
    smtp_send_command(&conn, "QUIT\r\n", 200, 299, "Closing SMTP session");
    smtp_connection_cleanup(&conn);
    return ret;
}

esp_err_t email_send_image(const uint8_t *image_data, size_t image_len)
{
    if (image_data == NULL || image_len == 0) {
        ESP_LOGE(TAG, "Invalid image data");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Sending image email, attachment size=%zu bytes", image_len);
    return smtp_send_email(SMTP_IMAGE_SUBJECT, SMTP_IMAGE_BODY, image_data, image_len);
}

esp_err_t email_send_camera_frame(const camera_fb_t *frame)
{
    if (frame == NULL || frame->buf == NULL || frame->len == 0) {
        ESP_LOGE(TAG, "Invalid camera frame");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Preparing frame for email: %zux%zu format=%d size=%zu bytes",
             frame->width, frame->height, frame->format, frame->len);

    if (frame->format != PIXFORMAT_JPEG) {
        ESP_LOGE(TAG, "Camera frame is not JPEG, refusing to re-encode. format=%d", frame->format);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return email_send_image(frame->buf, frame->len);
}

esp_err_t email_send_text(const char *message)
{
    if (message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Sending text email notification");
    return smtp_send_email(SMTP_TEXT_SUBJECT, message, NULL, 0);
}
