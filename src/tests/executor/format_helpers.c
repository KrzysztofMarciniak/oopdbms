/**
 * @file format_helpers.c
 * @brief Check executor format helpers against README.md byte layouts.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "implementation/executor/helper/binary_encoding.h"
#include "implementation/executor/helper/create_class_definition.h"
#include "implementation/executor/helper/create_field_descriptor.h"
#include "implementation/executor/helper/create_function_definition.h"
#include "implementation/executor/helper/create_header.h"
#include "implementation/executor/helper/create_interface_definition.h"
#include "implementation/executor/helper/create_metatable.h"
#include "implementation/executor/helper/create_object_record.h"
#include "implementation/executor/helper/create_table_entries.h"
#include "implementation/executor/helper/create_value_bytes.h"
#include "implementation/executor/helper/validate_metadata_layout.h"

static int checks;
static int failures;

#define CHECK(condition, description)                                       \
        do {                                                                \
                checks++;                                                   \
                if (!(condition)) {                                         \
                        failures++;                                         \
                        printf("FAIL: %s (%s:%d)\n", description, __FILE__, \
                               __LINE__);                                   \
                }                                                           \
        } while (0)

static void test_primitives_and_header(void) {
        const uint8_t expected_header[] = {'O', 'O', 'P', 'D', 'B', 0, 0, 0,
                                           1,   0,   2,   0,   3,   0, 0, 0};
        struct executor_format_header header  = {1, 2, 3};
        struct executor_format_header decoded = {0};
        uint8_t bytes[16]                     = {0};

        executor_helper_write_u64(bytes, UINT64_C(0x0123456789abcdef));
        CHECK(memcmp(bytes,
                     (const uint8_t[]){0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23,
                                       0x01},
                     8) == 0,
              "u64 is written little-endian");
        CHECK(executor_helper_read_u64(bytes) == UINT64_C(0x0123456789abcdef),
              "u64 is read little-endian");
        CHECK(executor_helper_create_header(bytes, sizeof bytes, &header),
              "header encoding succeeds");
        CHECK(memcmp(bytes, expected_header, sizeof expected_header) == 0,
              "header bytes follow the README layout");
        CHECK(executor_helper_read_header(bytes, sizeof bytes, &decoded) &&
                  decoded.interface_count == 1 && decoded.class_count == 2 &&
                  decoded.function_count == 3,
              "header decoding returns its counts");
        CHECK(!executor_helper_read_header(bytes, 15, &decoded),
              "truncated header is rejected");
        bytes[14] = 1;
        CHECK(!executor_helper_read_header(bytes, sizeof bytes, &decoded),
              "nonzero reserved header field is rejected");
}

static void test_field_and_value_encodings(void) {
        const uint8_t name[]               = {'r', 'e', 'f'};
        const uint8_t expected_field[]     = {0x04, 0x03, 0x02, 0x01, 5,
                                              3,    0,    0x0d, 0x0c, 0x0b,
                                              0x0a, 'r',  'e',  'f'};
        const uint8_t expected_float[]     = {0, 0, 0, 0, 0, 0, 0xf0, 0x3f};
        struct executor_format_field field = {
            .field_id        = UINT32_C(0x01020304),
            .type_code       = EXECUTOR_FORMAT_TYPE_POINTER,
            .target_class_id = UINT32_C(0x0a0b0c0d),
            .name            = name,
            .name_length     = sizeof name,
        };
        struct executor_format_field decoded = {0};
        uint8_t bytes[32]                    = {0};
        size_t written                       = 0;
        size_t offset                        = 0;

        CHECK(executor_helper_create_field_descriptor(bytes, sizeof bytes,
                                                      &field, &written) &&
                  written == sizeof expected_field &&
                  memcmp(bytes, expected_field, sizeof expected_field) == 0,
              "pointer field descriptor includes its target class ID");
        CHECK(executor_helper_read_field_descriptor(bytes, written, &offset,
                                                    &decoded) &&
                  offset == written && decoded.field_id == field.field_id &&
                  decoded.type_code == field.type_code &&
                  decoded.target_class_id == field.target_class_id &&
                  decoded.name_length == sizeof name &&
                  memcmp(decoded.name, name, sizeof name) == 0,
              "pointer field descriptor round-trips");
        offset = 0;
        CHECK(!executor_helper_read_field_descriptor(bytes, 10, &offset,
                                                     &decoded),
              "truncated pointer descriptor is rejected");
        CHECK(executor_helper_create_float_value(bytes, sizeof bytes, 1.0,
                                                 &written) &&
                  written == 8 &&
                  memcmp(bytes, expected_float, sizeof expected_float) == 0,
              "float is encoded as little-endian IEEE 754 binary64");
        CHECK(executor_helper_create_integer_value(bytes, sizeof bytes, -1,
                                                   &written) &&
                  written == 8 &&
                  memcmp(bytes,
                         (const uint8_t[]){0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                           0xff, 0xff},
                         8) == 0,
              "integer is encoded as signed 64-bit two's complement");
}

static void test_definitions_and_object_records(void) {
        const uint8_t class_name[]               = {'C'};
        const uint8_t field_name[]               = {'x'};
        const uint8_t function_name[]            = {'f'};
        const uint8_t parameter_name[]           = {'p'};
        const uint8_t expression[]               = {'p'};
        const uint8_t one[]                      = {1};
        const struct executor_format_field field = {
            .field_id    = 7,
            .type_code   = EXECUTOR_FORMAT_TYPE_BOOL,
            .name        = field_name,
            .name_length = sizeof field_name,
        };
        const struct executor_format_class_definition class_definition = {
            .name        = class_name,
            .name_length = sizeof class_name,
            .fields      = &field,
            .field_count = 1,
        };
        const struct executor_format_interface_definition interface_definition =
            {
                .name        = class_name,
                .name_length = sizeof class_name,
                .fields      = &field,
                .field_count = 1,
            };
        const struct executor_format_parameter parameter = {
            .name        = parameter_name,
            .name_length = sizeof parameter_name,
            .type_code   = EXECUTOR_FORMAT_TYPE_INTEGER,
        };
        const struct executor_format_function_definition function_definition = {
            .name              = function_name,
            .name_length       = sizeof function_name,
            .parameters        = &parameter,
            .parameter_count   = 1,
            .return_type_code  = EXECUTOR_FORMAT_TYPE_INTEGER,
            .expression        = expression,
            .expression_length = sizeof expression,
        };
        const struct executor_format_object_field object_field = {
            .field_id     = 7,
            .type_code    = EXECUTOR_FORMAT_TYPE_BOOL,
            .value        = one,
            .value_length = sizeof one,
        };
        const struct executor_format_object_record object = {
            .object_id   = 9,
            .fields      = &object_field,
            .field_count = 1,
        };
        const uint8_t expected_object[] = {0x1a, 0, 0, 0, 9, 0, 0, 0, 0,
                                           0,    0, 0, 0, 0, 1, 0, 7, 0,
                                           0,    0, 4, 1, 0, 0, 0, 1};
        uint8_t bytes[64]               = {0};
        size_t written                  = 0;
        struct executor_format_object_header decoded           = {0};
        struct executor_format_object_field_view decoded_field = {0};
        size_t offset                                          = 16;

        CHECK(executor_helper_create_class_definition(
                  bytes, sizeof bytes, &class_definition, &written) &&
                  written == 25 && bytes[0] == 1 && bytes[2] == 0 &&
                  bytes[4] == 0 && bytes[8] == 1 && bytes[16] == 'C',
              "class definition writes its fixed fields and declared field");
        CHECK(executor_helper_create_interface_definition(
                  bytes, sizeof bytes, &interface_definition, &written) &&
                  written == 13 && bytes[0] == 1 && bytes[2] == 1 &&
                  bytes[4] == 'C',
              "interface definition writes required fields");
        CHECK(executor_helper_create_function_definition(
                  bytes, sizeof bytes, &function_definition, &written) &&
                  written == 15 && bytes[0] == 1 && bytes[2] == 1 &&
                  bytes[4] == EXECUTOR_FORMAT_TYPE_INTEGER && bytes[5] == 1 &&
                  bytes[9] == 'f' && bytes[14] == 'p',
              "function definition writes signature and expression");
        CHECK(executor_helper_create_object_record(&bytes[0], sizeof bytes,
                                                   &object, &written) &&
                  written == sizeof expected_object &&
                  memcmp(bytes, expected_object, sizeof expected_object) == 0,
              "object record matches the 16-byte header and field layout");
        CHECK(executor_helper_read_object_header(bytes, written, &decoded) &&
                  decoded.record_length == written && decoded.object_id == 9 &&
                  decoded.field_count == 1,
              "object record header decodes");
        CHECK(executor_helper_read_object_field(bytes, written, &offset,
                                                &decoded_field) &&
                  offset == written && decoded_field.field_id == 7 &&
                  decoded_field.type_code == EXECUTOR_FORMAT_TYPE_BOOL &&
                  decoded_field.value_length == 1 &&
                  decoded_field.value[0] == 1,
              "object field decodes with its exact value bytes");
        bytes[25] = 2;
        offset    = 16;
        CHECK(!executor_helper_read_object_field(bytes, written, &offset,
                                                 &decoded_field),
              "invalid boolean value is rejected");
}

static void test_metadata_layout_validation(void) {
        const struct executor_format_header header = {0, 1, 0};
        struct executor_format_metatable metatable = {
            .classes = {.offset = 64, .length = 48},
        };

        CHECK(
            executor_helper_validate_metadata_layout(112, &header, &metatable),
            "metadata entry table fits in file");
        CHECK(
            !executor_helper_validate_metadata_layout(111, &header, &metatable),
            "metadata table extending past file is rejected");
        metatable.classes.offset = 63;
        CHECK(
            !executor_helper_validate_metadata_layout(112, &header, &metatable),
            "metadata table overlapping fixed header is rejected");
        metatable.classes.offset = 64;
        metatable.classes.length = 47;
        CHECK(
            !executor_helper_validate_metadata_layout(112, &header, &metatable),
            "metadata table length must match header count");
}

int main(void) {
        test_primitives_and_header();
        test_field_and_value_encodings();
        test_definitions_and_object_records();
        test_metadata_layout_validation();
        printf("executor format helper tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
