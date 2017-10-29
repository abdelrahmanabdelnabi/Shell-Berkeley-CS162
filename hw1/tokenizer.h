#pragma once

/* A struct that represents a list of words. */
struct tokens;

/* Turn a string into a list of words, string separated by any one delimiters. */
struct tokens *tokenize(const char *line,char *delimiters);

/* How many words are there? */
size_t tokens_get_length(struct tokens *tokens);

/* Get me the Nth word (zero-indexed) */
char *tokens_get_token(struct tokens *tokens, size_t n);

/* Free the memory */
void tokens_destroy(struct tokens *tokens);

/*utility function to print tokens joined into a string(used for debugging)*/
char* tokens_join(struct tokens *tokens, char join_char);