#pragma once
#include <stdbool.h>

// Starts Wi-Fi in station mode and blocks (up to ~15s) until connected.
// Returns true if an IP was obtained.
bool wifi_conn_start(void);

// True once we currently have an IP address.
bool wifi_conn_is_up(void);
