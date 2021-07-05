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
typedef void(* ParseFn)(bool canAssign);

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
 * Checks if the current token being pointed to is same as the one passed to it.
 * @param type Token to be checked against
 * @return true if tokens match, false otherwise.
 */
static bool check(TokenType type) {
    return parser.current.type == type;
}

/**
 * If the current token being parsed is same as the token passed to this function - it returns true and advances to the next token.
 * Returns false otherwise and doesn't advance the pointer.
 * @param type TokenType against which you want to check the current token being parsed.
 * @return true if tokens match, false otherwise.
 */
static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
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

static void statement();

static void declaration();

static uint8_t identifierConstant(Token* name);

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
static void binary(bool canAssign) {
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
static void literal(bool canAssign) {
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
static void grouping(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

/**
 * Function to compile number literals.
 */
static void number(bool canAssign) {
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
static void string(bool canAssign) {
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

/**
 * Emits the bytecode to read a global variable with a specific name.
 * Adds the name of the variable to the chunk's constant table as an ObjString, and stores it as an operand for OP_GET_GLOBAL in the bytecode.
 * @param name
 */
static void namedVariable(Token name, bool canAssign) {
    // Store the name if the token in the constant table, and store it's index in arg.
    uint8_t arg = identifierConstant(&name);

    // We check to see if this is an assignment or a get call.
    // In case of assignment, we'll see an '=' sign after the identifier.
    if (canAssign && match(TOKEN_EQUAL)) {
        // variable assignment.
        // we compile the assigned value.
        expression();
        // Emit the byte code for variable assignment along with that variable's name's ObjString representation's index in the constant table as the operand to the bytecode.
        emitBytes(OP_SET_GLOBAL, arg);
    } else {
        // Emit the byte code for reading a global variable along with that variable's name's ObjString representation's index in the constant table as the operand to the bytecode.
        emitBytes(OP_GET_GLOBAL, arg);
    }
}

/**
 * We use this function to hook up identifier tokens to the expression parser.
 */
static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

/**
 * Function to compile unary operations
 */
static void unary(bool canAssign) {
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
        [TOKEN_IDENTIFIER]    = {variable, NULL, PREC_NONE},
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

    // We don't want variable assignments breaking the precedence in cases such as a * b = c + d; where b might be assigned the value c + d.
    // So to take care of such cases, we pass in a boolean that tells variable() if it can perform assignment or not.
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    // If we had an invalid assignment target, we never would end up consuming the '='.
    // This is a syntax error, and we report it.
    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

/**
 * Takes a given token and adds its lexeme to the chunk's constant table as a string (ObjString).
 * Global Variables are looked up by name at runtime. This means the VM needs access to the name.
 * A whole string is too big to be stuffed into the bytecode stream as an operand, instead, it is stored as a string
 * in the constant table and instruction then refers to the name by its index in the table.
 * @param name Token to be used as the identifier.
 * @return Index of the constant (ObjString) in the constant table.
 */
static uint8_t identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

/**
 *  Parses a variable identifier.
 * @param errorMessage error message to be displayed when the next token isn't a TOKEN_IDENTIFIER
 * @return returns the index of the identifier in the constant table.
 */
static uint8_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    // returns the index of the identifier string (the variable name) in the constant table.
    return identifierConstant(&parser.previous);
}

/**
 * Outputs the bytecode instruction that defines a new variable and stores its initial value.
 * The index of the variable's name in the constant table is the instruction's operand.
 * @param global index of the variable's name in the chunk's constant table.
 */
static void defineVariable(uint8_t global) {
    emitBytes(OP_DEFINE_GLOBAL, global);
}

/**
 * Helper function to return a rule at a given index (according to the provided Token).
 * @param type
 * @return
 */
static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

/**
 * Parses a single expression, using the rule table with the help of parsePrecedence to take care of the precedence of operations.
 */
static void expression() {
    // We simply parse the lowest precedence level, which subsumes all of teh higher precedence expressions too.
    parsePrecedence(PREC_ASSIGNMENT);
}

/**
 * Parses a variable declaration.
 */
static void varDeclaration() {
    // The 'var' keyword is followed by the name of the variable, we compile that using parseVariable.
    // global stores the index of the identifier string (the variable name) present in the constant table.
    uint8_t global = parseVariable("Expect variable name.");

    // Then we look for an '='.
    if (match(TOKEN_EQUAL)) {
        // followed by an initializer expression.
        // This will emit bytecode for whatever value is going to be stored in the variable.
        expression();
    } else {
        // If the variable is not initialized, the compiler initializes it to 'nil'.
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    // emit the bytecode for defining a global.
    defineVariable(global);
}

/**
 * Parses an expression statement. Similar to parsing an expression, with an additional step to consume the semicolon and emits an OP_POP instruction.
 * Semantically, an expression statement evaluates the expression and discards teh result.
 * The compiler directly encodes that behaviour by emitting the OP_POP bytecode instruction.
 */
static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, " Expect ';' after expression.");
    emitByte(OP_POP);
}

/**
 * Parses the print statement. A print statement evaluates an expression and prints the result.
 * For this, it first parses and compiles the expression following the print token. After that it consumes the semicolon at the end.
 * Finally it emits the opcode for the print statement.
 */
static void printStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

/**
 * Synchronizes the compiler when it enters panic mode.
 * If we hit a compile error while parsing the previous statement, we enter panic mode.
 * When that happens, after the statement we start synchronizing.
 * Design Note: For Tok,statement boundaries are considered as a synchronization point.
 */
static void synchronize() {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {

        // we look for a statement boundary.
        if (parser.previous.type == TOKEN_SEMICOLON) return;

        // Control flow statements also start new statements,
        // so they are also treated as acceptable synchronization points.
        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;
            default:; // Do nothing.
        }

        // Advance to the next token.
        advance();
    }
}

/**
 * Parses a declaration.
 */
static void declaration() {
    if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        statement();
    }

    // We check in case the compiler is in panic mode, if so - we look for the next synchronization point.
    if (parser.panicMode) synchronize();
}

/**
 * Parses different kinds of statements that Tok supports.
 */
static void statement() {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else {
        expressionStatement();
    }
}

/**
 * Given the source code, this function compiles it to an intermediate bytecode representation.
 * @param source character array representing the source code
 * @param chunk Represents the current chunk being written. Acts like an output parameter.
 * @return true if compilation succeeds, false otherwise.
 */
bool compile(const char* source, Chunk* chunk) {
    initScanner(source);
    compilingChunk = chunk;

    parser.hadError = false;
    parser.panicMode = false;

    advance();
    while (!match(TOKEN_EOF)) {
        declaration();
    }

    endCompiler();
    return !parser.hadError;
}