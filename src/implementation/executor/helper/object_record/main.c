
#include "implementation/executor/helper/object_record/main.h"

#include <stdint.h>
#include <stdlib.h>

#include "implementation/executor/helper/basic/main.h"
#include "implementation/executor/helper/binary_primitives/main.h"

static int helper_object_valid_type_code(uint8_t type_code) {
        return type_code >= HELPER_OBJECT_TYPE_INTEGER &&
               type_code <= HELPER_OBJECT_TYPE_POINTER;
}

/*
 * Determine the serialized size of a field's value and validate
 * the corresponding in-memory value.
 */
static int helper_object_field_value_length(const helper_object_field* field,
                                            uint32_t* value_length) {
        if (field == NULL || value_length == NULL) {
                return -1;
        }

        switch (field->type_code) {
                case HELPER_OBJECT_TYPE_INTEGER:
                        *value_length = 8u;
                        return 0;

                case HELPER_OBJECT_TYPE_FLOAT:
                        *value_length = 8u;
                        return 0;

                case HELPER_OBJECT_TYPE_TEXT:
                        if (field->value.text.length > 0u &&
                            field->value.text.bytes == NULL) {
                                return -1;
                        }

                        *value_length = field->value.text.length;
                        return 0;

                case HELPER_OBJECT_TYPE_BOOL:
                        if (field->value.boolean > 1u) {
                                return -1;
                        }

                        *value_length = 1u;
                        return 0;

                case HELPER_OBJECT_TYPE_POINTER:
                        *value_length = HELPER_OBJECT_POINTER_VALUE_SIZE;
                        return 0;

                default:
                        return -1;
        }
}

static int helper_object_write_field_value(FILE* file,
                                           const helper_object_field* field,
                                           uint32_t value_length) {
        switch (field->type_code) {
                case HELPER_OBJECT_TYPE_INTEGER:
                        return helper_write_i64(file, field->value.integer);

                case HELPER_OBJECT_TYPE_FLOAT:
                        return helper_write_f64(file, field->value.floating);

                case HELPER_OBJECT_TYPE_TEXT:
                        if (value_length == 0u) {
                                return 0;
                        }

                        return helper_write_bytes(file, field->value.text.bytes,
                                                  value_length);

                case HELPER_OBJECT_TYPE_BOOL:
                        return helper_write_u8(file, field->value.boolean);

                case HELPER_OBJECT_TYPE_POINTER:
                        if (helper_write_u32(
                                file, field->value.pointer.class_id) != 0) {
                                return -1;
                        }

                        return helper_write_u64(file,
                                                field->value.pointer.object_id);

                default:
                        return -1;
        }
}

int helper_write_object_record(FILE* file, const helper_object_record* record) {
        uint64_t total_length = HELPER_OBJECT_RECORD_HEADER_SIZE;
        size_t i;

        if (file == NULL || record == NULL) {
                return -1;
        }

        if (record->field_count > 0u && record->fields == NULL) {
                return -1;
        }

        /*
         * Validate the entire record and compute its total length before
         * writing anything. Each field contributes a 9-byte field header
         * followed by its value.
         */
        for (i = 0; i < (size_t)record->field_count; ++i) {
                uint32_t value_length;

                if (!helper_object_valid_type_code(
                        record->fields[i].type_code)) {
                        return -1;
                }

                if (helper_object_field_value_length(&record->fields[i],
                                                     &value_length) != 0) {
                        return -1;
                }

                total_length += (uint64_t)HELPER_OBJECT_FIELD_HEADER_SIZE +
                                (uint64_t)value_length;

                if (total_length > UINT32_MAX) {
                        return -1;
                }
        }

        /* Object record header: 16 bytes. */
        if (helper_write_u32(file, (uint32_t)total_length) != 0) {
                return -1;
        }

        if (helper_write_u64(file, record->object_id) != 0) {
                return -1;
        }

        if (helper_write_u16(file, record->flags) != 0) {
                return -1;
        }

        if (helper_write_u16(file, record->field_count) != 0) {
                return -1;
        }

        /* Serialize each field record. */
        for (i = 0; i < (size_t)record->field_count; ++i) {
                const helper_object_field* field = &record->fields[i];
                uint32_t value_length;

                /*
                 * This was checked in the validation pass above.
                 */
                if (helper_object_field_value_length(field, &value_length) !=
                    0) {
                        return -1;
                }

                if (helper_write_u32(file, field->field_id) != 0) {
                        return -1;
                }

                if (helper_write_u8(file, field->type_code) != 0) {
                        return -1;
                }

                if (helper_write_u32(file, value_length) != 0) {
                        return -1;
                }

                if (helper_object_write_field_value(file, field,
                                                    value_length) != 0) {
                        return -1;
                }
        }

        return 0;
}

void helper_free_object_record(helper_object_record* record) {
        size_t i;

        if (record == NULL) {
                return;
        }

        if (record->fields != NULL) {
                for (i = 0; i < (size_t)record->field_count; ++i) {
                        if (record->fields[i].type_code ==
                            HELPER_OBJECT_TYPE_TEXT) {
                                free(record->fields[i].value.text.bytes);
                                record->fields[i].value.text.bytes  = NULL;
                                record->fields[i].value.text.length = 0u;
                        }
                }

                free(record->fields);
                record->fields = NULL;
        }

        record->object_id   = 0u;
        record->flags       = 0u;
        record->field_count = 0u;
}

static int helper_object_read_field_value(FILE* file,
                                          helper_object_field* field,
                                          uint32_t value_length) {
        switch (field->type_code) {
                case HELPER_OBJECT_TYPE_INTEGER:
                        if (value_length != 8u) {
                                return -1;
                        }

                        return helper_read_i64(file, &field->value.integer);

                case HELPER_OBJECT_TYPE_FLOAT:
                        if (value_length != 8u) {
                                return -1;
                        }

                        return helper_read_f64(file, &field->value.floating);

                case HELPER_OBJECT_TYPE_TEXT:
                        field->value.text.length = value_length;

                        if (value_length == 0u) {
                                field->value.text.bytes = NULL;
                                return 0;
                        }

                        field->value.text.bytes = malloc((size_t)value_length);
                        if (field->value.text.bytes == NULL) {
                                return -1;
                        }

                        return helper_read_bytes(file, field->value.text.bytes,
                                                 value_length);

                case HELPER_OBJECT_TYPE_BOOL:
                        if (value_length != 1u) {
                                return -1;
                        }

                        if (helper_read_u8(file, &field->value.boolean) != 0) {
                                return -1;
                        }

                        if (field->value.boolean > 1u) {
                                return -1;
                        }

                        return 0;

                case HELPER_OBJECT_TYPE_POINTER:
                        if (value_length != HELPER_OBJECT_POINTER_VALUE_SIZE) {
                                return -1;
                        }

                        if (helper_read_u32(
                                file, &field->value.pointer.class_id) != 0) {
                                return -1;
                        }

                        return helper_read_u64(file,
                                               &field->value.pointer.object_id);

                default:
                        return -1;
        }
}

int helper_read_object_record(FILE* file, helper_object_record* record) {
        helper_object_record result = {0};
        uint32_t record_length;
        uint64_t remaining;
        size_t i;

        if (file == NULL || record == NULL) {
                return -1;
        }

        if (helper_read_u32(file, &record_length) != 0) {
                goto failure;
        }

        if (record_length < HELPER_OBJECT_RECORD_HEADER_SIZE) {
                goto failure;
        }

        if (helper_read_u64(file, &result.object_id) != 0) {
                goto failure;
        }

        if (helper_read_u16(file, &result.flags) != 0) {
                goto failure;
        }

        if (helper_read_u16(file, &result.field_count) != 0) {
                goto failure;
        }

        remaining = (uint64_t)record_length - HELPER_OBJECT_RECORD_HEADER_SIZE;

        if (result.field_count > 0u) {
                result.fields =
                    calloc((size_t)result.field_count, sizeof(*result.fields));

                if (result.fields == NULL) {
                        goto failure;
                }
        }

        for (i = 0; i < (size_t)result.field_count; ++i) {
                helper_object_field* field = &result.fields[i];
                uint32_t value_length;

                /*
                 * A field header contains field_id (4), type_code (1),
                 * and value_length (4): 9 bytes.
                 */
                if (remaining < HELPER_OBJECT_FIELD_HEADER_SIZE) {
                        goto failure;
                }

                if (helper_read_u32(file, &field->field_id) != 0) {
                        goto failure;
                }

                if (helper_read_u8(file, &field->type_code) != 0) {
                        goto failure;
                }

                if (helper_read_u32(file, &value_length) != 0) {
                        goto failure;
                }

                remaining -= HELPER_OBJECT_FIELD_HEADER_SIZE;

                if (!helper_object_valid_type_code(field->type_code)) {
                        goto failure;
                }

                if ((uint64_t)value_length > remaining) {
                        goto failure;
                }

                /*
                 * Validate the length against the type and read the value.
                 * The remaining-byte check above prevents consuming bytes
                 * beyond the declared record boundary.
                 */
                switch (field->type_code) {
                        case HELPER_OBJECT_TYPE_INTEGER:
                                if (value_length != 8u) {
                                        goto failure;
                                }
                                break;

                        case HELPER_OBJECT_TYPE_FLOAT:
                                if (value_length != 8u) {
                                        goto failure;
                                }
                                break;

                        case HELPER_OBJECT_TYPE_BOOL:
                                if (value_length != 1u) {
                                        goto failure;
                                }
                                break;

                        case HELPER_OBJECT_TYPE_POINTER:
                                if (value_length !=
                                    HELPER_OBJECT_POINTER_VALUE_SIZE) {
                                        goto failure;
                                }
                                break;

                        case HELPER_OBJECT_TYPE_TEXT:
                                break;

                        default:
                                goto failure;
                }

                if (helper_object_read_field_value(file, field, value_length) !=
                    0) {
                        goto failure;
                }

                remaining -= value_length;
        }

        /*
         * The fields must consume the complete declared record.
         * Reject inconsistent record lengths and trailing bytes.
         */
        if (remaining != 0u) {
                goto failure;
        }

        *record = result;
        return 0;

failure:
        helper_free_object_record(&result);
        return -1;
}
