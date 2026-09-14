#pragma once

#include <stdbool.h>

bool spiffs_mount_or_abort(void);
const char *spiffs_root_path(void);
void spiffs_print_usage(void);
