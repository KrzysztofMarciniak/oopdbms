#ifndef IMPLEMENTATION_EXECUTOR_HELPER_TYPE_NAME_MAIN_H
#define IMPLEMENTATION_EXECUTOR_HELPER_TYPE_NAME_MAIN_H

#include <stddef.h>

#include "interface/analyzer/statement.h"

size_t helper_type_name(const struct analyzer_type* type, char* out,
                        size_t out_size);

#endif
