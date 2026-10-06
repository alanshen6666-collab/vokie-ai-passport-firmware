#pragma once

#include "cJSON.h"

// Optional host_text control message: chunked reply text for the on-device
// overlay. Firmware that supports this advertises it in hello as
// "features":["host_text"].
//
// The BLE host task calls this with the parsed control message. Invalid or
// out-of-order chunks are ignored; a complete message is sanitized and
// forwarded to ui_status_show_reply().
void host_text_handle_message(const cJSON *root);
