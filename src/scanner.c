//
// Created by Jyotinder Singh on 04/06/21.
//

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct {
    // Marks the beginning of the current lexeme being scanned.
    const char* start;
    // Marks the current character being looked at.
    const char* current;
    // Tracks the current line number the current lexeme is on for error reporting.
    int line;
} Scanner;

Scanner scanner;

void initScanner(const char* source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

/**
 * Utility function to check if a character is an alphabet or not.
 * @param c
 * @return
 */
static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

/**
 * Utility function to check if a character is a digit or not.
 * @param c
 * @return
 */
static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

/**
 * Helper function to check if we are at the end of the source code.
 * @return
 */
static bool isAtEnd() {
    return *scanner.current == '\0';
}

/**
 * Helper function to consume and return the character that scanner.current is pointing towards.
 * @return
 */
static char advance() {
    scanner.current++;
    return scanner.current[-1];
}

/**
 * Function which returns the current character being pointed at, but doesn't consume it.
 * @return
 */
static char peek() {
    return *scanner.current;
}

/**
 * Function for 1 character lookahead.
 * @return
 */
static char peekNext() {
    if (isAtEnd()) return '\0';
    return scanner.current[1];
}

/**
 * Helper function to check if the next character which is ready to be consumed (pointed to by scanner.current) matches the expected character.
 * @param expected
 * @return
 */
static bool match(char expected) {
    if (isAtEnd()) return false;
    if (*scanner.current != expected) return false;
    scanner.current++;
    return true;
}

/**
 * Function to create and return a new Token of a given TokenType.
 * @param type
 * @return
 */
static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int) (scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

/**
 * Function to create and return an Error Token.
 * NOTE: The message argument should only be called on C literals to avoid memory leaks.
 * @param message
 * @return
 */
static Token errorToken(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int) strlen(message);
    token.line = scanner.line;
    return token;
}

/**
 * Function to handle and skip all the whitespace and comments.
 */
static void skipWhitespace() {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
                // newline
            case '\n':
                scanner.line++;
                advance();
                break;
                // newline
                // comment
            case '/':
                if (peekNext() == '/') {
                    // A comment goes until the end of the line.
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else {
                    return;
                }
                break;
                // comment
            default:
                return;
        }
    }
}

static TokenType checkKeyword(int start, int length, const char* rest, TokenType type) {
    if (scanner.current - scanner.start == start + length && memcmp(scanner.start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

/**
 * A helper function to return the proper token type for an identifier/reserved keyword.
 * @return
 */
static TokenType identifierType() {
    switch (scanner.start[0]) {
        case 'a':
            return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'c':
            return checkKeyword(1, 4, "lass", TOKEN_CLASS);
        case 'e':
            return checkKeyword(1, 3, "lse", TOKEN_ELSE);
        case 'f':
            // Before we switch we need to check if there is even a second character, since 'f' by itself would also be a valid identifier.
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a':
                        return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o':
                        return checkKeyword(2, 1, "r", TOKEN_FOR);
                    case 'u':
                        return checkKeyword(2, 1, "n", TOKEN_FUN);
                }
            }
            break;
        case 'i':
            return checkKeyword(1, 1, "f", TOKEN_IF);
        case 'n':
            return checkKeyword(1, 2, "il", TOKEN_NIL);
        case 'o':
            return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p':
            return checkKeyword(1, 4, "rint", TOKEN_PRINT);
        case 'r':
            return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 's':
            return checkKeyword(1, 4, "uper", TOKEN_SUPER);
        case 't':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'h':
                        return checkKeyword(2, 2, "is", TOKEN_THIS);
                    case 'r':
                        return checkKeyword(2, 2, "ue", TOKEN_TRUE);
                }
            }
            break;
        case 'v':
            return checkKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w':
            return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

/**
 * Function to handle identifiers.
 * @return
 */
static Token identifier() {
    while (isAlpha(peek()) || isDigit(peek())) advance();
    return makeToken(identifierType());
}

/**
 * Function to handle number literals.
 * @return
 */
static Token number() {
    while (isDigit(peek())) advance();

    // Look for a fractional part.
    if (peek() == '.' && isDigit(peekNext())) {
        // Consume the ".".
        advance();

        while (isDigit(peek())) advance();
    }

    return makeToken(TOKEN_NUMBER);
}

/**
 * Function to handle string literals.
 * @return
 */
static Token string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') scanner.line++;
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated String.");

    // The closing quote.
    advance();
    return makeToken(TOKEN_STRING);
}

/**
 * Main function which scans the tokens and returns the corresponding tokens for each group of characters it encounters.
 * @return
 */
Token scanToken() {
    skipWhitespace();
    /**
     * Since each call to this function scans a complete token, we know we are at teh beginning of a new token
     * when we enter the function. Thus we set scanner.start to the current character so we remember
     * where the lexeme we're about to scan starts.
     */
    scanner.start = scanner.current;
    if (isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();
    if (isAlpha(c)) return identifier();
    if (isDigit(c)) return number();

    switch (c) {
        case '(':
            return makeToken(TOKEN_LEFT_PAREN);
        case ')':
            return makeToken(TOKEN_RIGHT_PAREN);
        case '{':
            return makeToken(TOKEN_LEFT_BRACE);
        case '}':
            return makeToken(TOKEN_RIGHT_BRACE);
        case ';':
            return makeToken(TOKEN_SEMICOLON);
        case ',':
            return makeToken(TOKEN_COMMA);
        case '.':
            return makeToken(TOKEN_DOT);
        case '-':
            return makeToken(TOKEN_MINUS);
        case '+':
            return makeToken(TOKEN_PLUS);
        case '/':
            return makeToken(TOKEN_SLASH);
        case '*':
            return makeToken(TOKEN_STAR);
        case '!':
            return makeToken(
                    match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            return makeToken(
                    match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<':
            return makeToken(
                    match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return makeToken(
                    match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '"':
            return string();
    }

    return errorToken("Unexpected character.");
}