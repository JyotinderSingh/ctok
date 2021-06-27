//
// Created by Jyotinder Singh on 04/06/21.
//

#ifndef CTOK_SCANNER_H
#define CTOK_SCANNER_H

/**
 * The different kinds of tokens that Tok supports.
 */
typedef enum {
    // Single-character tokens.
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR,
    // One or two character tokens.
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    // Literals.
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,
    // Keywords.
    TOKEN_AND, TOKEN_CLASS, TOKEN_ELSE, TOKEN_FALSE,
    TOKEN_FOR, TOKEN_FUN, TOKEN_IF, TOKEN_NIL, TOKEN_OR,
    TOKEN_PRINT, TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS,
    TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE,

    TOKEN_ERROR, TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;

    /**
     * Note:
     * We don't store the lexeme for the Token as a string in the struct itself, since it would make memory management difficult.
     * That's especially hard since we pass tokens by value - multiple tokens could point to the same lexeme String.
     * Ownership gets weird.
     * Instead, we use the original source string as our character store.
     * We represent a lexeme by a pointer to its first character and the number of characters it contains.
     */
    const char* start;
    int length;
    int line;
} Token;

void initScanner(const char* source);

Token scanToken();

#endif //CTOK_SCANNER_H
