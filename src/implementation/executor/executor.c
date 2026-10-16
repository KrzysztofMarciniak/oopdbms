/**
 * @file executor.c
 * @brief Executor dispatch: group, then kind, then the command's function.
 */

#include "interface/executor/executor.h"

#include <stddef.h>

#include "implementation/executor/executor_internal.h"

enum EXECUTOR_RESULT i_execute(struct database* db,
                               const struct analyzer_statement* statement) {
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        if (db == NULL || statement == NULL) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        switch (statement->group) {
                case ANALYZER_GROUP_SCHEMA:
                        switch (statement->kind) {
                                case ANALYZER_CREATE_CLASS:
                                        result =
                                            execute_create_class(db, statement);
                                        break;
                                case ANALYZER_DROP_CLASS:
                                        result =
                                            execute_drop_class(db, statement);
                                        break;
                                case ANALYZER_CREATE_INTERFACE:
                                        result = execute_create_interface(
                                            db, statement);
                                        break;
                                case ANALYZER_DROP_INTERFACE:
                                        result = execute_drop_interface(
                                            db, statement);
                                        break;
                                case ANALYZER_ALTER_CLASS_ADD_FIELD:
                                        result = execute_alter_class_add_field(
                                            db, statement);
                                        break;
                                case ANALYZER_ALTER_CLASS_DROP_FIELD:
                                        result = execute_alter_class_drop_field(
                                            db, statement);
                                        break;
                                case ANALYZER_ALTER_CLASS_RENAME_FIELD:
                                        result =
                                            execute_alter_class_rename_field(
                                                db, statement);
                                        break;
                                case ANALYZER_ALTER_CLASS_RENAME:
                                        result = execute_alter_class_rename(
                                            db, statement);
                                        break;
                                case ANALYZER_ALTER_CLASS_EXTENDS:
                                        result = execute_alter_class_extends(
                                            db, statement);
                                        break;
                                case ANALYZER_ALTER_CLASS_IMPLEMENTS:
                                        result = execute_alter_class_implements(
                                            db, statement);
                                        break;
                                default:
                                        return EXECUTOR_ERROR_BAD_STATEMENT;
                        }
                        break;

                case ANALYZER_GROUP_FUNCTION:
                        switch (statement->kind) {
                                case ANALYZER_CREATE_FUNCTION:
                                        result = execute_create_function(
                                            db, statement);
                                        break;
                                case ANALYZER_DROP_FUNCTION:
                                        result = execute_drop_function(
                                            db, statement);
                                        break;
                                case ANALYZER_CALL:
                                        result = execute_call(db, statement);
                                        break;
                                default:
                                        return EXECUTOR_ERROR_BAD_STATEMENT;
                        }
                        break;

                case ANALYZER_GROUP_OBJECT:
                        switch (statement->kind) {
                                case ANALYZER_NEW:
                                        result = execute_new(db, statement);
                                        break;
                                case ANALYZER_FIND:
                                        result = execute_find(db, statement);
                                        break;
                                case ANALYZER_SET:
                                        result = execute_set(db, statement);
                                        break;
                                case ANALYZER_DELETE:
                                        result = execute_delete(db, statement);
                                        break;
                                default:
                                        return EXECUTOR_ERROR_BAD_STATEMENT;
                        }
                        break;

                case ANALYZER_GROUP_METADATA:
                        switch (statement->kind) {
                                case ANALYZER_LIST_CLASSES:
                                        result =
                                            execute_list_classes(db, statement);
                                        break;
                                case ANALYZER_LIST_INTERFACES:
                                        result = execute_list_interfaces(
                                            db, statement);
                                        break;
                                case ANALYZER_DESCRIBE:
                                        result =
                                            execute_describe(db, statement);
                                        break;
                                default:
                                        return EXECUTOR_ERROR_BAD_STATEMENT;
                        }
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
