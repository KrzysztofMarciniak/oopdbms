/**
 * @file executor.c
 * @brief Dispatch checked statements to executor feature modules.
 */

#include "implementation/executor/executor_internal.h"

static bool _kind_matches_group(const struct analyzer_statement* statement) {
        if (statement == NULL) {
                return false;
        }
        switch (statement->group) {
                case ANALYZER_GROUP_SCHEMA:
                        return statement->kind == ANALYZER_CREATE_CLASS ||
                               statement->kind == ANALYZER_DROP_CLASS ||
                               statement->kind ==
                                   ANALYZER_ALTER_CLASS_ADD_FIELD ||
                               statement->kind ==
                                   ANALYZER_ALTER_CLASS_DROP_FIELD ||
                               statement->kind ==
                                   ANALYZER_ALTER_CLASS_RENAME_FIELD ||
                               statement->kind == ANALYZER_ALTER_CLASS_RENAME ||
                               statement->kind ==
                                   ANALYZER_ALTER_CLASS_EXTENDS ||
                               statement->kind ==
                                   ANALYZER_ALTER_CLASS_IMPLEMENTS ||
                               statement->kind == ANALYZER_CREATE_INTERFACE ||
                               statement->kind == ANALYZER_DROP_INTERFACE;
                case ANALYZER_GROUP_FUNCTION:
                        return statement->kind == ANALYZER_CREATE_FUNCTION ||
                               statement->kind == ANALYZER_DROP_FUNCTION ||
                               statement->kind == ANALYZER_CALL;
                case ANALYZER_GROUP_OBJECT:
                        return statement->kind == ANALYZER_NEW ||
                               statement->kind == ANALYZER_FIND ||
                               statement->kind == ANALYZER_SET ||
                               statement->kind == ANALYZER_DELETE;
                case ANALYZER_GROUP_METADATA:
                        return statement->kind == ANALYZER_LIST_CLASSES ||
                               statement->kind == ANALYZER_LIST_INTERFACES ||
                               statement->kind == ANALYZER_DESCRIBE;
                default:
                        return false;
        }
}

static bool _kind_mutates_database(enum ANALYZER_STATEMENT kind) {
        switch (kind) {
                case ANALYZER_CREATE_CLASS:
                case ANALYZER_DROP_CLASS:
                case ANALYZER_ALTER_CLASS_ADD_FIELD:
                case ANALYZER_ALTER_CLASS_DROP_FIELD:
                case ANALYZER_ALTER_CLASS_RENAME_FIELD:
                case ANALYZER_ALTER_CLASS_RENAME:
                case ANALYZER_ALTER_CLASS_EXTENDS:
                case ANALYZER_ALTER_CLASS_IMPLEMENTS:
                case ANALYZER_CREATE_INTERFACE:
                case ANALYZER_DROP_INTERFACE:
                case ANALYZER_CREATE_FUNCTION:
                case ANALYZER_DROP_FUNCTION:
                case ANALYZER_NEW:
                case ANALYZER_SET:
                case ANALYZER_DELETE:
                        return true;
                default:
                        return false;
        }
}

enum EXECUTOR_RESULT i_execute(struct database* db,
                               const struct analyzer_statement* statement) {
        enum EXECUTOR_RESULT result;

        if (db == NULL || !_kind_matches_group(statement)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (db->format_mode && !db->read_only &&
            _kind_mutates_database(statement->kind) &&
            !executor_format_file_migrate(db)) {
                return EXECUTOR_ERROR_SAVE_FAILED;
        }
        switch (statement->group) {
                case ANALYZER_GROUP_SCHEMA:
                        result = executor_schema_execute(db, statement);
                        break;
                case ANALYZER_GROUP_FUNCTION:
                        result = executor_functions_execute(db, statement);
                        break;
                case ANALYZER_GROUP_OBJECT:
                        result = executor_objects_execute(db, statement);
                        break;
                case ANALYZER_GROUP_METADATA:
                        result = executor_metadata_execute(db, statement);
                        break;
                default:
                        return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (result == EXECUTOR_OK && database_is_modified(db) &&
            !database_save(db)) {
                result = EXECUTOR_ERROR_SAVE_FAILED;
        }
        return result;
}
