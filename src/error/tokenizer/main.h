#ifndef ERROR_TOKENIZER_MAIN_H
#define ERROR_TOKENIZER_MAIN_H

enum tokenizer_error_type { TOKENIZER_OK = 0 };

struct tokenizer_error {
        enum tokenizer_error_type type;
};

void tokenizer_error_print(struct tokenizer_error te);

#endif
