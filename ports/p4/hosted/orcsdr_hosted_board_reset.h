#pragma once
#include <stdbool.h>
#include "eh_host_port_err.h"

bool orcsdr_hosted_board_reset_available(void);
eh_host_port_err_t orcsdr_hosted_board_reset(void);
