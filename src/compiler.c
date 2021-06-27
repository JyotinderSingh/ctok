//
// Created by Jyotinder Singh on 04/06/21.
//

#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE

#include "debug.h"

#endif

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;

/*
 * ParseFn is just a typedef for a function type that takes no arguments and returns nothing.
 */
typedef void(* ParseFn)();

/**
 * Structure that defines each row of our parsing table.
 */
typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

Parser parser;
Chunk* compilingChunk;

/**
 * Utility method to return a pointer to the current chunk being compiled.
 * @return
 */
static Chunk* currentChunk() {
    return compilingChunk;
}

/**
 * The main function that handles the errors and outputs the relevant text to the standard error stream.
 * @param token
 * @param message
 */
static void errorAt(Token* token, const char* message) {
    // If the parser is in panic mode, we skip displaying useless cascading error messages.
    if (parser.panicMode) return;

    // if we encounter a parsing error, we enter panic mode.
    parser.panicMode = true;
    // We print where the error occurred.
    fprintf(stderr, "[line %d] Error", token->line);

    // We try to show the lexeme if it’s human-readable.
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    // We print the error message itself.
    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

/**
 * Often, we need to report an error at the location of the token we just consumed, we use this function for the same.
 * @param message
 */
static void error(const char* message) {
    errorAt(&parser.previous, message);
}

/**
 * When the scanner hands the parser an error token, we need to notify the user about this event using this function.
 * @param message
 */
static void errorAtCurrent(const char* message) {
    // We pull out the location of the current token to tell the user where the error actually occurred.
    errorAt(&parser.current, message);
}

/**
 * Function that steps forward through the token stream. It asks the scanner for the next token and stores it for later use.
 * Before doing that, it takes the old 'current' token and stashes that in a previous field, which comes in handy later
 * so we can get at the lexeme after we match a token.
 */
static void advance() {
    parser.previous = parser.current;

    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

/**
 * Function that is similar in effect to advance(), but it also validates that the token has an expected type.
 * If not, it reports an error.
 * @param type
 * @param message
 */
static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

/**
 * After we parse and understand a piece of the source code, the next step is to translate it into a series of
 * bytecode instructions. We start this off by appending a single byte to the chunk.
 * @param byte
 */
static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

/**
 * Often we need to write an opcode followed by a one-byte operand - this function handles those cases.
 * @param byte1
 * @param byte2
 */
static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitReturn() {
    emitByte(OP_RETURN);
}

/**
 * This function adds the given value to the end of the chunk's constant table and returns its index.
 * The function's job is mostly to make sure we don't have too many constants.
 * Since the OP_CONSTANT instruction uses a single byte for the index operand,
 * we can store and load only upto 256 constants in a chunk.
 * @param value
 * @return
 */
static uint8_t makeConstant(Value value) {
    // We get the index of the constant in the ValueArray after pushing it there.
    int constant = addConstant(currentChunk(), value);
    // In case we overflowed our limit for maximum number of constants in one chunk (256), we report this error.
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    // If everything went well, we return the index of the constant in the ValueArray.
    return (uint8_t) constant;
}

/**
 * Firstly we add the value to the constant table using a call to makeConstant(), then we emit an OP_CONSTANT
 * instruction that pushes it onto the stack at runtime.
 * @param value
 */
static void emitConstant(Value value) {
    // We emit bytes for the opcode (OP_CONSTANT) and it's operand (index of the corresponding constant).
    emitBytes(OP_CONSTANT, makeConstant(value));
}

/**
 * Clean up function.
 */
static void endCompiler() {
    emitReturn();
#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), "code");
    }
#endif
}

// Forward declarations.
static void expression();

static ParseRule* getRule(TokenType type);

static void parsePrecedence(Precedence precedence);


/**
 * Function to be used for infix parsing.
 * When an infix parsing function is called, the entire left hand operation has already been compiled,
 * and the subsequent infix operator has already been consumed.
 * The fact that the left operator gets consumed first works out fine, it means that code gets executed first.
 * When it runs the, the value it produces ends up on the stack, which is right where the infix operator is going to need it.
 * After that we come here to this function.
 * When the VM is run, it will execute the left and the right operand code, in that order - leaving their values on the stack.
 * Then it executes the instruction for this operator - that pops the two values, performs the computation, and pushes the result.
 */
static void binary() {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    // Parse the right operand.
    parsePrecedence((Precedence) (rule->precedence + 1));

    // Finally, emit the bytecode instruction that will perform the binary operation.
    switch (operatorType) {
        case TOKEN_BANG_EQUAL:
            emitBytes(OP_EQUAL, OP_NOT);
            break;
        case TOKEN_EQUAL_EQUAL:
            emitByte(OP_EQUAL);
            break;
        case TOKEN_GREATER:
            emitByte(OP_GREATER);
            break;
        case TOKEN_GREATER_EQUAL:
            emitBytes(OP_LESS, OP_NOT);
            break;
        case TOKEN_LESS:
            emitByte(OP_LESS);
            break;
        case TOKEN_LESS_EQUAL:
            emitBytes(OP_GREATER, OP_NOT);
            break;
        case TOKEN_PLUS:
            emitByte(OP_ADD);
            break;
        case TOKEN_MINUS:
            emitByte(OP_SUBTRACT);
            break;
        case TOKEN_STAR:
            emitByte(OP_MULTIPLY);
            break;
        case TOKEN_SLASH:
            emitByte(OP_DIVIDE);
            break;
        default:
            return; //unreachable.
    }
}

/**
 * Function to handle literals.
 */
static void literal() {
    switch (parser.previous.type) {
        case TOKEN_FALSE:
            emitByte(OP_FALSE);
            break;
        case TOKEN_NIL:
            emitByte(OP_NIL);
            break;
        case TOKEN_TRUE:
            emitByte(OP_TRUE);
            break;
        default:
            return;
    }
}

/**
 * Function to compile grouping expressions.
 * Assumes TOKEN_LEFT_PAREN has already been consumed.
 */
static void grouping() {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

/**
 * Function to compile number literals.
 */
static void number() {
    /**
     * We assume the token for the number literal has already been consumed and is stored in 'previous'.
     * We then take that lexeme and use the C stdlib to convert it to a double value.
     */
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

/**
 * When the parser hits a string token, it calls this parse function.
 * The +1 and -2 parts trim the leading and trailing quotation marks. It then creates a string object,
 * wraps it in a Value, and adds it to the constant table.
 */
static void string() {
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

/**
 * Function to compile unary operations
 */
static void unary() {
    // The leading token gets stored into 'operatorType'.
    TokenType operatorType = parser.previous.type;

    /**
     * Compile the operand.
     * We evaluate the operand first which leaves its value on the stack.
     * Later (inside the VM) we pop the value, perform the unary operation on it, and push the result.
     */
    parsePrecedence(PREC_UNARY);

    // Emit the operator instruction.
    switch (operatorType) {
        case TOKEN_BANG:
            emitByte(OP_NOT);
            break;
        case TOKEN_MINUS:
            emitByte(OP_NEGATE);
            break;
        default:
            return; //unreachable
    }
}

/**
 * Defines the table that drives our entire parser. Given a token type, it lets us find:
 * - the function to compile a prefix expression starting with a token of that type.
 * - the function to compile an infix expression whiles left operand is followed by a token of that type.
 * - the precedence of an infix expression that uses that token as an operator.
 */
ParseRule rules[] = {
        [TOKEN_LEFT_PAREN]    = {grouping, NULL, PREC_NONE},
        [TOKEN_RIGHT_PAREN]   = {NULL, NULL, PREC_NONE},
        [TOKEN_LEFT_BRACE]    = {NULL, NULL, PREC_NONE},
        [TOKEN_RIGHT_BRACE]   = {NULL, NULL, PREC_NONE},
        [TOKEN_COMMA]         = {NULL, NULL, PREC_NONE},
        [TOKEN_DOT]           = {NULL, NULL, PREC_NONE},
        [TOKEN_MINUS]         = {unary, binary, PREC_TERM},
        [TOKEN_PLUS]          = {NULL, binary, PREC_TERM},
        [TOKEN_SEMICOLON]     = {NULL, NULL, PREC_NONE},
        [TOKEN_SLASH]         = {NULL, binary, PREC_FACTOR},
        [TOKEN_STAR]          = {NULL, binary, PREC_FACTOR},
        [TOKEN_BANG]          = {unary, NULL, PREC_NONE},
        [TOKEN_BANG_EQUAL]    = {NULL, binary, PREC_NONE},
        [TOKEN_EQUAL]         = {NULL, NULL, PREC_NONE},
        [TOKEN_EQUAL_EQUAL]   = {NULL, binary, PREC_EQUALITY},
        [TOKEN_GREATER]       = {NULL, binary, PREC_COMPARISON},
        [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
        [TOKEN_LESS]          = {NULL, binary, PREC_COMPARISON},
        [TOKEN_LESS_EQUAL]    = {NULL, binary, PREC_COMPARISON},
        [TOKEN_IDENTIFIER]    = {NULL, NULL, PREC_NONE},
        [TOKEN_STRING]        = {string, NULL, PREC_NONE},
        [TOKEN_NUMBER]        = {number, NULL, PREC_NONE},
        [TOKEN_AND]           = {NULL, NULL, PREC_NONE},
        [TOKEN_CLASS]         = {NULL, NULL, PREC_NONE},
        [TOKEN_ELSE]          = {NULL, NULL, PREC_NONE},
        [TOKEN_FALSE]         = {literal, NULL, PREC_NONE},
        [TOKEN_FOR]           = {NULL, NULL, PREC_NONE},
        [TOKEN_FUN]           = {NULL, NULL, PREC_NONE},
        [TOKEN_IF]            = {NULL, NULL, PREC_NONE},
        [TOKEN_NIL]           = {literal, NULL, PREC_NONE},
        [TOKEN_OR]            = {NULL, NULL, PREC_NONE},
        [TOKEN_PRINT]         = {NULL, NULL, PREC_NONE},
        [TOKEN_RETURN]        = {NULL, NULL, PREC_NONE},
        [TOKEN_SUPER]         = {NULL, NULL, PREC_NONE},
        [TOKEN_THIS]          = {NULL, NULL, PREC_NONE},
        [TOKEN_TRUE]          = {literal, NULL, PREC_NONE},
        [TOKEN_VAR]           = {NULL, NULL, PREC_NONE},
        [TOKEN_WHILE]         = {NULL, NULL, PREC_NONE},
        [TOKEN_ERROR]         = {NULL, NULL, PREC_NONE},
        [TOKEN_EOF]           = {NULL, NULL, PREC_NONE},
};

/**
 * This function starts at the current token and parses any expression at the given precedence level or higher.
 * We read the next token and look up the corresponding ParseRule.
 * If there is no prefix parser then the token must be a syntax error.
 * After parsing that, which may consume more tokens, the prefix expression is done. Now we look for an infix parser for the next token.
 * If we find one, it means the prefix expression might be an operand for it. But only if the call to parsePrecedence()
 * has a precedence that is low enough to permit that infix operator.
 * @param precedence
 */
static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }

    prefixRule();

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule();
    }
}

/**
 * Helper function to return a rule at a given index (according to the provided Token).
 * @param type
 * @return
 */
static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

static void expression() {
    // We simply parse the lowest precedence level, which subsumes all of teh higher precedence expressions too.
    parsePrecedence(PREC_ASSIGNMENT);
}

bool compile(const char* source, Chunk* chunk) {
    initScanner(source);
    compilingChunk = chunk;

    parser.hadError = false;
    parser.panicMode = false;

    advance();
    expression();
    consume(TOKEN_EOF, "Expect end of expression.");
    endCompiler();
    return !parser.hadError;
}