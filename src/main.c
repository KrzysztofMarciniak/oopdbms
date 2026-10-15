#include "interface/help/main.h"

int main(int argc, char** argv) {
        if (argc < 2) {
                i_print_help();
        }
        // 1. tokenizer
        // 2. (syntax) analyzer
        // 2.1 wrong->print error
        // 2.2 correct->next
        // 3.	 file_check
        // 4.  execute
}
