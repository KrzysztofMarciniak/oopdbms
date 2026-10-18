#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int test_analyzer_main(void);
int test_executor_commands(void);
int test_executor_create_class(void);
int test_executor_create_class_extends(void);
int test_executor_create_class_implements_interface(void);
int test_executor_create_class_pointer(void);
int test_executor_create_function(void);
int test_executor_create_function_pointer(void);
int test_executor_create_interface(void);
int test_executor_format_helpers(void);
int test_executor_list_classes(void);
int test_executor_list_interfaces(void);
int test_executor_main(void);
int test_executor_schema_mutations(void);
int test_file_check_main(void);
int test_storage_main(void);
int test_tokenizer_main(void);

struct test_suite {
        const char* id;
        const char* group;
        const char* name;
        int (*run)(void);
};

static const struct test_suite suites[] = {
    {"analyzer", "Language", "Analyzer", test_analyzer_main},
    {"tokenizer", "Language", "Tokenizer", test_tokenizer_main},
    {"file-check", "Database", "File check", test_file_check_main},
    {"storage", "Database", "Storage", test_storage_main},
    {"executor", "Executor", "Routing", test_executor_main},
    {"commands", "Executor", "Commands", test_executor_commands},
    {"format-helpers", "Executor", "Format helpers",
     test_executor_format_helpers},
    {"create-class", "Executor", "Create class", test_executor_create_class},
    {"class-extends", "Executor", "Class inheritance",
     test_executor_create_class_extends},
    {"class-interface", "Executor", "Class interfaces",
     test_executor_create_class_implements_interface},
    {"class-pointer", "Executor", "Class pointers",
     test_executor_create_class_pointer},
    {"create-function", "Executor", "Create function",
     test_executor_create_function},
    {"function-pointer", "Executor", "Function pointers",
     test_executor_create_function_pointer},
    {"create-interface", "Executor", "Create interface",
     test_executor_create_interface},
    {"list-classes", "Executor", "List classes", test_executor_list_classes},
    {"list-interfaces", "Executor", "List interfaces",
     test_executor_list_interfaces},
    {"schema-mutations", "Executor", "Schema mutations",
     test_executor_schema_mutations},
};

#define SUITE_COUNT (sizeof suites / sizeof suites[0])

static int _colors_enabled(void) {
        return isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL;
}

static void _usage(const char* program) {
        printf("Usage: %s [--list | --suite ID | --help]\n", program);
        printf("Run all suites by default; use --list to show suite IDs.\n");
}

static void _list_suites(void) {
        size_t i;

        for (i = 0; i < SUITE_COUNT; i++) {
                printf("%-20s %s / %s\n", suites[i].id, suites[i].group,
                       suites[i].name);
        }
}

int main(int argc, char** argv) {
        const char* requested_suite = NULL;
        const char* current_group   = NULL;
        const char* green           = "";
        const char* red             = "";
        const char* cyan            = "";
        const char* reset           = "";
        size_t selected             = 0;
        size_t passed               = 0;
        size_t failed               = 0;
        size_t i;

        if (argc == 2 && strcmp(argv[1], "--list") == 0) {
                _list_suites();
                return 0;
        }
        if (argc == 2 && strcmp(argv[1], "--help") == 0) {
                _usage(argv[0]);
                return 0;
        }
        if (argc == 3 && strcmp(argv[1], "--suite") == 0) {
                requested_suite = argv[2];
        } else if (argc != 1) {
                _usage(argv[0]);
                return 2;
        }
        if (requested_suite != NULL) {
                for (i = 0; i < SUITE_COUNT; i++) {
                        if (strcmp(requested_suite, suites[i].id) == 0) {
                                break;
                        }
                }
                if (i == SUITE_COUNT) {
                        fprintf(stderr, "Unknown test suite: %s\n",
                                requested_suite);
                        fprintf(stderr,
                                "Use --list to see available suites.\n");
                        return 2;
                }
        }

        if (_colors_enabled()) {
                green = "\033[32m";
                red   = "\033[31m";
                cyan  = "\033[36m";
                reset = "\033[0m";
        }

        printf("+--------------------------------------------------+\n");
        printf("|                 OOPDBMS TESTS                   |\n");
        printf("+--------------------------------------------------+\n");

        for (i = 0; i < SUITE_COUNT; i++) {
                int result;

                if (requested_suite != NULL &&
                    strcmp(requested_suite, suites[i].id) != 0) {
                        continue;
                }
                selected++;
                if (current_group == NULL ||
                    strcmp(current_group, suites[i].group) != 0) {
                        current_group = suites[i].group;
                        printf("\n%s[%s]%s\n", cyan, current_group, reset);
                }

                printf("  %-24s ", suites[i].name);
                fflush(stdout);
                result = suites[i].run();
                if (result == 0) {
                        passed++;
                        printf("%sPASS%s\n", green, reset);
                } else {
                        failed++;
                        printf("%sFAIL%s\n", red, reset);
                }
        }

        printf("\n+--------------------------------------------------+\n");
        printf("| %zu passed  |  %zu failed  |  %zu total\n", passed, failed,
               selected);
        printf("+--------------------------------------------------+\n");
        return failed == 0 ? 0 : 1;
}
