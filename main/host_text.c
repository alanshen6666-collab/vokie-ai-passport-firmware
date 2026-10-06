#include "host_text.h"

#include "ui_status.h"

#include "esp_log.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "host_text";

// Bounds mirror the documented host_text contract: at most 16 chunks and
// 2048 assembled bytes. A chunk must fit one 182-byte control payload, so
// 128 text bytes is already an upper bound the host never reaches.
#define HOST_TEXT_MAX_CHUNKS 16
#define HOST_TEXT_CHUNK_BYTES 128
#define HOST_TEXT_MAX_BYTES 2048

static char s_text[HOST_TEXT_MAX_BYTES + 1];
static uint32_t s_message_id;
static uint8_t s_expected;
static uint16_t s_received;
static size_t s_length;
static bool s_active;

static void reset_assembly(void)
{
    s_active = false;
    s_expected = 0;
    s_received = 0;
    s_length = 0;
}

// Replace control characters and invalid UTF-8 so LVGL always receives
// well-formed displayable text. Newlines and tabs are kept.
static void sanitize(char *text)
{
    const unsigned char *source = (const unsigned char *)text;
    size_t length = strlen(text);
    size_t read = 0;
    size_t write = 0;
    while (read < length)
    {
        unsigned char first = source[read];
        size_t sequence = 0;
        if (first < 0x80) sequence = 1;
        else if ((first & 0xE0) == 0xC0) sequence = 2;
        else if ((first & 0xF0) == 0xE0) sequence = 3;
        else if ((first & 0xF8) == 0xF0) sequence = 4;

        bool valid = sequence > 0 && read + sequence <= length;
        for (size_t offset = 1; valid && offset < sequence; ++offset)
        {
            if ((source[read + offset] & 0xC0) != 0x80) valid = false;
        }
        if (!valid)
        {
            text[write++] = '?';
            read += 1;
            continue;
        }
        for (size_t offset = 0; offset < sequence; ++offset)
        {
            text[write++] = (char)source[read + offset];
        }
        read += sequence;
    }
    text[write] = '\0';

    for (size_t index = 0; index < write; ++index)
    {
        unsigned char value = (unsigned char)text[index];
        if ((value < 0x20 && value != '\n' && value != '\t') || value == 0x7F)
            text[index] = ' ';
    }
}

void host_text_handle_message(const cJSON *root)
{
    const cJSON *id = cJSON_GetObjectItem(root, "id");
    const cJSON *chunk = cJSON_GetObjectItem(root, "chunk");
    const cJSON *count = cJSON_GetObjectItem(root, "count");
    const cJSON *text = cJSON_GetObjectItem(root, "text");
    if (!cJSON_IsNumber(id) || !cJSON_IsNumber(chunk) || !cJSON_IsNumber(count) ||
        !cJSON_IsString(text))
    {
        ESP_LOGW(TAG, "ignoring malformed host_text message");
        return;
    }

    const double id_value = id->valuedouble;
    const int chunk_index = chunk->valueint;
    const int chunk_count = count->valueint;
    const size_t chunk_length = strlen(text->valuestring);
    if (!(id_value >= 0.0 && id_value < 4294967296.0) || chunk_count < 1 ||
        chunk_count > HOST_TEXT_MAX_CHUNKS || chunk_index < 0 ||
        chunk_index >= chunk_count || chunk_length == 0 ||
        chunk_length > HOST_TEXT_CHUNK_BYTES)
    {
        ESP_LOGW(TAG, "ignoring invalid host_text chunk");
        return;
    }

    const uint32_t message_id = (uint32_t)id_value;
    if (!s_active || message_id != s_message_id || chunk_count != s_expected)
    {
        // Only chunk zero may start a message; anything else would append to
        // a message this host did not begin.
        if (chunk_index != 0)
        {
            reset_assembly();
            return;
        }
        s_message_id = message_id;
        s_expected = (uint8_t)chunk_count;
        s_received = 0;
        s_length = 0;
        s_active = true;
    }
    else if (chunk_index < (int)s_received)
    {
        return; // duplicate chunk
    }
    else if (chunk_index > (int)s_received)
    {
        reset_assembly(); // out-of-order chunk; wait for a fresh message
        return;
    }

    if (s_length + chunk_length > HOST_TEXT_MAX_BYTES)
    {
        ESP_LOGW(TAG, "host_text message exceeds %d bytes; dropped",
                 HOST_TEXT_MAX_BYTES);
        reset_assembly();
        return;
    }
    memcpy(s_text + s_length, text->valuestring, chunk_length);
    s_length += chunk_length;
    s_received += 1;

    if (s_received == s_expected)
    {
        s_text[s_length] = '\0';
        sanitize(s_text);
        ESP_LOGI(TAG, "reply text ready: %u bytes in %u chunks",
                 (unsigned)s_length, (unsigned)s_expected);
        ui_status_show_reply(s_text);
        reset_assembly();
    }
}
