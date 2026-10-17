#include "error/executor/executor_error.h"

#include <stddef.h>
#include <stdio.h>

/** Statement names, indexed by statement kind. */
static const char* const STATEMENT_NAMES[] = {
    [ANALYZER_CREATE_CLASS]             = "CREATE CLASS",
    [ANALYZER_DROP_CLASS]               = "DROP CLASS",
    [ANALYZER_ALTER_CLASS_ADD_FIELD]    = "ALTER CLASS ADD FIELD",
    [ANALYZER_ALTER_CLASS_DROP_FIELD]   = "ALTER CLASS DROP FIELD",
    [ANALYZER_ALTER_CLASS_RENAME_FIELD] = "ALTER CLASS RENAME FIELD",
    [ANALYZER_ALTER_CLASS_RENAME]       = "ALTER CLASS RENAME",
    [ANALYZER_ALTER_CLASS_EXTENDS]      = "ALTER CLASS EXTENDS",
    [ANALYZER_ALTER_CLASS_IMPLEMENTS]   = "ALTER CLASS IMPLEMENTS",
    [ANALYZER_CREATE_INTERFACE]         = "CREATE INTERFACE",
    [ANALYZER_DROP_INTERFACE]           = "DROP INTERFACE",
    [ANALYZER_CREATE_FUNCTION]          = "CREATE FUNCTION",
    [ANALYZER_DROP_FUNCTION]            = "DROP FUNCTION",
    [ANALYZER_CALL]                     = "CALL",
    [ANALYZER_NEW]                      = "NEW",
    [ANALYZER_FIND]                     = "FIND",
    [ANALYZER_SET]                      = "SET",
    [ANALYZER_DELETE]                   = "DELETE",
    [ANALYZER_LIST_CLASSES]             = "LIST CLASSES",
    [ANALYZER_LIST_INTERFACES]          = "LIST INTERFACES",
    [ANALYZER_DESCRIBE]                 = "DESCRIBE",
};

/** Name of a statement kind for messages. */
static const char* _name_of(enum ANALYZER_STATEMENT kind) {
        size_t index = (size_t)kind;

        if (index >= sizeof STATEMENT_NAMES / sizeof STATEMENT_NAMES[0] ||
            STATEMENT_NAMES[index] == NULL) {
                return "statement";
        }
        return STATEMENT_NAMES[index];
}

void executor_error_print(enum EXECUTOR_RESULT result,
                          enum ANALYZER_STATEMENT kind) {
        switch (result) {
                case EXECUTOR_OK:
                        puts("executor: OK");
                        break;
                case EXECUTOR_ERROR_BAD_STATEMENT:
                        fputs("executor: cannot run this statement\n", stderr);
                        break;
                case EXECUTOR_ERROR_NOT_IMPLEMENTED:
                        fprintf(stderr, "executor: %s is not implemented yet\n",
                                _name_of(kind));
                        break;
                case EXECUTOR_ERROR_STORAGE_FAILED:
                        fprintf(stderr,
                                "executor: %s failed while reading database "
                                "metadata\n",
                                _name_of(kind));
                        break;
                case EXECUTOR_ERROR_SAVE_FAILED:
                        fprintf(stderr,
                                "executor: %s ran but saving the database "
                                "failed\n",
                                _name_of(kind));
                        break;
                default:
                        puts("Unknown executor error");
                        break;
        }
}
