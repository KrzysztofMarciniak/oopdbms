#ifndef IMPLEMENTATION_EXECUTOR_EXECUTOR_INTERNAL_H
#define IMPLEMENTATION_EXECUTOR_EXECUTOR_INTERNAL_H

/**
 * @file executor_internal.h
 * @brief One function per command, called only by the executor dispatch.
 *
 * Each function receives a statement of its own kind, so it can read the
 * fields listed in its documentation without checking the kind again. A
 * function that changes data calls database_mark_modified().
 */

#include "error/executor/executor_error.h"
#include "interface/analyzer/statement.h"
#include "interface/storage/database.h"

/**
 * @brief CREATE CLASS <n> [EXTENDS <class>] [IMPLEMENTS <interface>[, ...]]
 * (<field> <type>[, ...])
 *
 * Uses: name, parent, implements, fields.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_create_class(
    struct database* db, const struct analyzer_statement* statement);

/**
 * @brief DROP CLASS <n>
 *
 * Uses: name.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_drop_class(
    struct database* db, const struct analyzer_statement* statement);

/**
 * @brief CREATE INTERFACE <n> (<field> <type>[, ...])
 *
 * Uses: name, fields.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_create_interface(
    struct database* db, const struct analyzer_statement* statement);

/**
 * @brief DROP INTERFACE <n>
 *
 * Uses: name.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_drop_interface(
    struct database* db, const struct analyzer_statement* statement);

/**
 * @brief ALTER CLASS <n> ADD FIELD <field> <type>
 *
 * Uses: name, fields (exactly one).
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_alter_class_add_field(
    struct database* db, const struct analyzer_statement* statement);

/**
 * @brief ALTER CLASS <n> DROP FIELD <field>
 *
 * Uses: name, field.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_alter_class_drop_field(
    struct database* db, const struct analyzer_statement* statement);

/**
 * @brief ALTER CLASS <n> RENAME FIELD <old> <new>
 *
 * Uses: name, field (old name), new_name.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_alter_class_rename_field(
    struct database* db, const struct analyzer_statement* statement);

/**
 * @brief ALTER CLASS <n> RENAME <new>
 *
 * Uses: name, new_name.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_alter_class_rename(
    struct database* db, const struct analyzer_statement* statement);

/**
 * @brief ALTER CLASS <n> EXTENDS <class>
 *
 * Uses: name, parent.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_alter_class_extends(
    struct database* db, const struct analyzer_statement* statement);

/**
 * @brief ALTER CLASS <n> IMPLEMENTS <interface>
 *
 * Uses: name, implements (exactly one).
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_alter_class_implements(
    struct database* db, const struct analyzer_statement* statement);

/**
 * @brief CREATE FUNCTION <class>.<n> (<arg> <type>[, ...]) RETURNS <type> AS
 * <expression>
 *
 * Uses: name (class), function, fields (arguments), return_type, expression.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_create_function(
    struct database* db, const struct analyzer_statement* statement);

/**
 * @brief DROP FUNCTION <class>.<n>
 *
 * Uses: name (class), function.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_drop_function(
    struct database* db, const struct analyzer_statement* statement);

/**
 * @brief CALL <class>.<n> (<value>[, ...]) ON <id>
 *
 * Uses: name (class), function, values, id.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_call(struct database* db,
                                  const struct analyzer_statement* statement);

/**
 * @brief NEW <class> (<field> = <value>[, ...])
 *
 * Uses: name (class), assignments.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_new(struct database* db,
                                 const struct analyzer_statement* statement);

/**
 * @brief FIND <class> [WHERE <condition>]
 *
 * Uses: name (class), expression (NULL without WHERE).
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_find(struct database* db,
                                  const struct analyzer_statement* statement);

/**
 * @brief SET <class> <id> (<field> = <value>[, ...])
 *
 * Uses: name (class), id, assignments.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_set(struct database* db,
                                 const struct analyzer_statement* statement);

/**
 * @brief DELETE <class> <id>
 *
 * Uses: name (class), id.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_delete(struct database* db,
                                    const struct analyzer_statement* statement);

/**
 * @brief LIST CLASSES
 *
 * Uses: nothing.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_list_classes(
    struct database* db, const struct analyzer_statement* statement);

/**
 * @brief LIST INTERFACES
 *
 * Uses: nothing.
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_list_interfaces(
    struct database* db, const struct analyzer_statement* statement);

/**
 * @brief DESCRIBE <class>
 *
 * Uses: name (class).
 *
 * @param db        Database to run on.
 * @param statement Checked statement.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT execute_describe(
    struct database* db, const struct analyzer_statement* statement);

#endif// IMPLEMENTATION_EXECUTOR_EXECUTOR_INTERNAL_H
