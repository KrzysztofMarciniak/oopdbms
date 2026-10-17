/**
 * @file create_value_bytes.h
 * @brief Encode scalar values using the type encodings in README.md.
 */

#ifndef IMPLEMENTATION_EXECUTOR_HELPER_CREATE_VALUE_BYTES_H
#define IMPLEMENTATION_EXECUTOR_HELPER_CREATE_VALUE_BYTES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool executor_helper_create_integer_value(uint8_t* output, size_t capacity,
                                          int64_t value, size_t* written);
bool executor_helper_create_float_value(uint8_t* output, size_t capacity,
                                        double value, size_t* written);
bool executor_helper_create_text_value(uint8_t* output, size_t capacity,
                                       const uint8_t* value, size_t length,
                                       size_t* written);
bool executor_helper_create_bool_value(uint8_t* output, size_t capacity,
                                       bool value, size_t* written);
bool executor_helper_create_pointer_value(uint8_t* output, size_t capacity,
                                          uint32_t target_class_id,
                                          uint64_t target_object_id,
                                          size_t* written);

#endif
