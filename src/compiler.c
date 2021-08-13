//
// Created by Jyotinder Singh on 04/06/21.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
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

/**
 * Data structure backing the local variables.
 */
typedef struct {
    // The token name is used to compare the identifier's lexeme with each local's name to find a match.
    Token name;
    // The depth field records the scope depth of the block where the local variable was declared.
    int depth;
    // Tells if this particular local is captured by some closure. This field is true if the local is captured by any later nested function declaration.
    bool isCaptured;
} Local;

/**
 * Data structure to maintain a list of upvalues for the closures.
 */
typedef struct {
    // stores info about which local slot the upvalue is capturing.
    uint8_t index;
    // the isLocal flag controls whether the closure captures a local variable or an upvalue from the surrounding function.
    bool isLocal;
} Upvalue;

/**
 * Enum to tell the compiler when it is compiling top-level code vs the body of a function.
 */
typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT
} FunctionType;

/**
 * Data structure to back the compiler implementation.
 * A flat array of all the locals that are in the scope during each point in the compilation
 * process. They are ordered in the array in the order in which their declarations appear in the code.
 * Since the instruction operand we use to encode a local is a single byte, the VM has a hard limit
 * on the number of locals that can be in scope at once. This is the reason we can give our locals
 * a fixed size.
 * Our top level code lives inside an automatically defined 'top-level' function. That way, the compiler is always
 * within some kind of function body, and the VM always runs code by invoking a function.
 * This makes our implementation a little simpler.
 */
typedef struct Compiler {
    // Keeps track of the reference to the enclosing compiler instance.
    struct Compiler* enclosing;
    // implicit top-level function for the top-level code.
    ObjFunction* function;
    FunctionType type;
    // Array to track the names of the variables in the current scope.
    Local locals[UINT8_COUNT];
    // tracks how many locals are in scope i.e. how many array slots are in use.
    int localCount;
    // Maintans upvalue references for closures.
    Upvalue upvalues[UINT8_COUNT];
    // scopeDepth tracks the number of blocks surrounding the current bit of code being compiled.
    int scopeDepth;
} Compiler;

Parser parser;
Compiler* current = NULL;
Chunk* compilingChunk;

/**
 * Utility method to return a pointer to the current chunk being compiled.
 * The current chunk is always the chunk owned by the function we're in the middle of compiling.
 * @return
 */
static Chunk* currentChunk() {
    // return a reference to the chunk of the current function we are compiling(top-level or otherwise).
    return &current->function->chunk;
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
 * @param byte bytecode instruction / operand to be emitted
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

/**
 * Function to emit a looping instruction.
 * Emits a new loop instruction, which unconditionally jumps backwards by a given offset.
 * We calculate the offset from teh instruction we're currently at to the loopStart point that we want to jump back to.
 * @param loopStart
 */
static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);

    // +2 is to take into account the size of the OP_LOOP instruction's own operands, which we also need to jump over.
    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large. I know this sucks, please bear with me.");

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

/**
 * Emits a backpatching jump instruction.
 * The function first emits a bytecode instruction and writes a placeholder operand for the jump offset.
 * We use two bytes for the jump offset operand. A 16-bit jump offset lets us jump over 65,536 bytes of code.
 * @param instruction bytecode instruction to be generated
 * @return offset of the emitted instruction in the chunk.
 */
static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void emitReturn() {
    // implicit return value of any function is NIL.
    emitByte(OP_NIL);
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
 * Patches the incomplete jump instruction emitted by emitJump.
 * It goes back to the bytecode and replaces the operand at the given location with the calculated jump offset.
 * We call patchJump() right before we emit the next instruction that we want the jump to land on,
 * so it uses the current bytecode count to determine how far to jump.
 * @param offset distance by which it should jump back by, to patch the emitJump operand.
 */
static void patchJump(int offset) {
    // -2 to adjust for the bytecode for the jump offset itself.
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    // break the 16 bit offset into two 8 bit pieces.
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

/**
 * Initializes the compiler for whatever kind of function we are compiling.
 * @param compiler
 */
static void initCompiler(Compiler* compiler, FunctionType type) {
    // Save a reference to the enclosing compiler instance.
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    /**
     * We create an ObjFunction in the compiler itself. Even though its a runtime representation of the function.
     * The way to think of it is that a function is similar to a string or a number literal. It forms a bridge between
     * the compile-time and runtime worlds. When you reach a function declaration - they produce a value of a built in type (ObjFunction).
     * So the compiler creates the function objects during compilation. Then, at runtime, they are simply invoked.
     */
    compiler->function = newFunction();
    current = compiler;

    // we call initCompiler right after we parse the function's name. That means we can simply grab the name from the previous token.
    if (type != TYPE_SCRIPT) {
        // NOTE: we create a copy of the name string. Since the lexeme points straight to the source code string.
        // The string may get freed once the code is finished compiling. The function object we create in the compiler outlives
        // the compiler and persists in realtime. So it needs its own heap-allocated name string that it can keep around.
        current->function->name = copyString(parser.previous.start, parser.previous.length);
    }

    /**
     * The compiler's <code>locals</code> array keeps track of which stack slots are associated with which local variables
     * or temporaries. The compiler implicitly claims stack slot zero for the VM's own internal use.
     * We give it an empty name so that the user can't write an identifier that refers to it.
     */
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    local->name.start = "";
    local->name.length = 0;
}

/**
 * Clean up function.
 */
static ObjFunction* endCompiler() {
    emitReturn();
    ObjFunction* function = current->function;
#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        // We check if the name of the function is null. User defined functions have names, but the implicit
        // top level function does not.
        disassembleChunk(currentChunk(), function->name != NULL ? function->name->chars : "<script>");
    }
#endif
    // restore the enclosing compiler instance.
    current = current->enclosing;
    return function;
}

/**
 * Function called before entering a new scope.
 */
static void beginScope() {
    current->scopeDepth++;
}

/**
 * Function called when exiting a scope.
 * Decreases the scopeDepth by 1, and frees up all the local variables for the exiting scope.
 */
static void endScope() {
    current->scopeDepth--;

    // Clear out exiting scope's variables.
    while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth) {
        // We can tell which variables are closed over and need to get hoisted up onto the stack.
        if (current->locals[current->localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            // if variable is not closed over, we can pop it off the stack.
            emitByte(OP_POP);
        }
        current->localCount--;
    }
}

// Forward declarations.
static void expression();

static void statement();

static void declaration();

static int resolveLocal(Compiler* compiler, Token* name);

static int resolveUpvalue(Compiler* compiler, Token* name);

static uint8_t identifierConstant(Token* name);

static ParseRule* getRule(TokenType type);

static void parsePrecedence(Precedence precedence);

static void and_(bool canAssign);

static uint8_t argumentList();

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
 * Method to handle Tok function calls.
 * We've already consumed the '(' when calling this, so next we compile the arguments.
 * We get back the number of arguments from <code>argumentList()</code>.
 * Each argument expression generates code that leaves its value on the stack in preparation for the next call.
 * After that, we emit a new OP_CALL instruction to invoke the function, using the argument count as an operand.
 * @param canAssign
 */
static void call(bool canAssign) {
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
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
 * Method to handle getter and setter operations on a class instance.
 * @param canAssign
 */
static void dot(bool canAssign) {
    // The parser expects to find a property name immediately after the dot.
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    // We load that token's lexeme into the constant table as a string so that the name is available at runtime.
    uint8_t name = identifierConstant(&parser.previous);

    // if we see an equals sign after the field name, it must be a set expression that is assigning to a field.
    // The canAssign makes sure we don't allow illegal assignments like:
    // a + b.c = 3
    // if we didn't catch this, it would be interpreted as a + (b.c = 3)
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(OP_SET_PROPERTY, name);
    } else {
        emitBytes(OP_GET_PROPERTY, name);
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
 * Function to compile logical 'or' operator.
 * In an 'or' expression if teh left-hand side is truthy, then we skip over the right operand.
 * Thus we need to jump when value is truthy.
 * When the left-hand side is falsey, it does a tiny jump over the next statement. That statement is an unconditional
 * jump over the code for the right operand. The little dance effectively does a jump when the value is truthy.
 * @param canAssign
 */
static void or_(bool canAssign) {
    // Jump to the right-hand operand if left-hand is false.
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    // VM will reach this bytecode instruction if the left hand value was true.
    // This will make the instruction pointer jump to the end of the conditional expression (Since we already found one truthy value).
    int endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    // Pop the value if left-hand side expression was falsey to make space for right-hand operand.
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
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
    // We set the appropriate bytecode for local/global variables depending on the operation being performed.
    uint8_t getOp, setOp;
    // Try to locate the local variable with the given name. If we find one, we use the instructions for working
    // with locals. Otherwise, we assume we're working with globals.
    int arg = resolveLocal(current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        // The resolveUpvalue() function looks for a local variable declared in any of the surrounding functions.
        // If it finds one, it returns an "upvalue index" for that variable, otherwise it returns -1 to indicate the variable wasn't found.
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    // We check to see if this is an assignment or a get call.
    // In case of assignment, we'll see an '=' sign after the identifier.
    if (canAssign && match(TOKEN_EQUAL)) {
        // variable assignment.
        // we compile the assigned value.
        expression();
        // Emit the byte code for variable assignment along with that variable's name's ObjString representation's index in the constant table as the operand to the bytecode.
        emitBytes(setOp, (uint8_t) arg);
    } else {
        // Emit the byte code for reading a variable along with that variable's name's ObjString representation's index in the constant table as the operand to the bytecode.
        emitBytes(getOp, (uint8_t) arg);
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
        [TOKEN_LEFT_PAREN]    = {grouping, call, PREC_CALL},
        [TOKEN_RIGHT_PAREN]   = {NULL, NULL, PREC_NONE},
        [TOKEN_LEFT_BRACE]    = {NULL, NULL, PREC_NONE},
        [TOKEN_RIGHT_BRACE]   = {NULL, NULL, PREC_NONE},
        [TOKEN_COMMA]         = {NULL, NULL, PREC_NONE},
        [TOKEN_DOT]           = {NULL, dot, PREC_CALL},
        [TOKEN_MINUS]         = {unary, binary, PREC_TERM},
        [TOKEN_PLUS]          = {NULL, binary, PREC_TERM},
        [TOKEN_SEMICOLON]     = {NULL, NULL, PREC_NONE},
        [TOKEN_SLASH]         = {NULL, binary, PREC_FACTOR},
        [TOKEN_STAR]          = {NULL, binary, PREC_FACTOR},
        [TOKEN_BANG]          = {unary, NULL, PREC_NONE},
        [TOKEN_BANG_EQUAL]    = {NULL, binary, PREC_EQUALITY},
        [TOKEN_EQUAL]         = {NULL, NULL, PREC_NONE},
        [TOKEN_EQUAL_EQUAL]   = {NULL, binary, PREC_EQUALITY},
        [TOKEN_GREATER]       = {NULL, binary, PREC_COMPARISON},
        [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
        [TOKEN_LESS]          = {NULL, binary, PREC_COMPARISON},
        [TOKEN_LESS_EQUAL]    = {NULL, binary, PREC_COMPARISON},
        [TOKEN_IDENTIFIER]    = {variable, NULL, PREC_NONE},
        [TOKEN_STRING]        = {string, NULL, PREC_NONE},
        [TOKEN_NUMBER]        = {number, NULL, PREC_NONE},
        [TOKEN_AND]           = {NULL, and_, PREC_AND},
        [TOKEN_CLASS]         = {NULL, NULL, PREC_NONE},
        [TOKEN_ELSE]          = {NULL, NULL, PREC_NONE},
        [TOKEN_FALSE]         = {literal, NULL, PREC_NONE},
        [TOKEN_FOR]           = {NULL, NULL, PREC_NONE},
        [TOKEN_FUN]           = {NULL, NULL, PREC_NONE},
        [TOKEN_IF]            = {NULL, NULL, PREC_NONE},
        [TOKEN_NIL]           = {literal, NULL, PREC_NONE},
        [TOKEN_OR]            = {NULL, or_, PREC_OR},
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
 * Utility function to check whether the lexemes of two Tokens (namely variables) are equal/same.
 * @param a first Token
 * @param b second Token
 * @return True if tokens' lexemes are equal, false otherwise.
 */
static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

/**
 * Function to resolve a local variable in the current scope.
 * At runtime, we load and store the locals using the stack slot index, so that's what the compiler needs to calculate after
 * it resolves the variable. Whenever a variable is declared, we append it to the locals array in the Compiler.
 * This means the first local variable is at index zero, next one at index one, and so on.
 * In other words, the locals array in the compiler has the exact same layout as the VM's stack will have at runtime.
 * The variable's index in the locals array is the same as its stack slot.
 * @param compiler reference to the current compiler object.
 * @param name Name of the variable being looked up
 * @return Returns the index of the variable on the VM's stack, returns -1 if variable was not found.
 */
static int resolveLocal(Compiler* compiler, Token* name) {
    // Walk the list of locals that are currently in scope. We walk the list backwards to make sure inner local variables shadow the locals with the same name in surrounding scopes.
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        // if a variable in the locals list has the same name as the variable being looked up, we've found what we are looking for.
        if (identifiersEqual(name, &local->name)) {
            // If the depth of the local being scanned is set to -1, it means that it is only declared right now, and not defined.
            // We report an error in this case.
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

/**
 * The compiler maintains an array of upvalue structures to track the closed over identifiers that it has resolved in the
 * body of each function. We know the compiler's Local array mirrors the stack stack slot indexes where the locals live at runtime.
 * The upvalue array works the same way. The indexes in the compiler's array match the indexes where the upvalues will
 * live in ObjClosure at runtime.
 * The function adds a new upvalue to the array. It also keeps track of the number of upvalues the function uses.
 * It stores this count directly in the ObjFunction because we need this number at runtime as well.
 * The index field tracks the closed-over local variable's slot index. That way the compiler knows which variable in the
 * enclosing function needs to be captured.
 * @param compiler instance of the compiler to which the upvalue reference is to be added to
 * @param index index of the upvalue in the enclosing function's Local array
 * @param isLocal
 * @return index of the created upvalue in the function's upvalue list. This index is operand to OP_GET_UPVALUE & OP_SET_UPVALUE.
 */
static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;

    // Since a function might reference the same variable in the surrounding function multiple times. In that case, we don't
    // want to waste time and memory creating a separate upvalue for each identifier expression. So, before adding a new upvalue
    // we first check to see if the function already has an upvalue that closes over that variable.
    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    // Check for overflow of upvalues array.
    if (upvalueCount == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

/**
 * Function to resolve a variable in an enclosing function's scope.
 * We call this after failing to resolve a local variable in the current function's scope, so we know the variable isn't in the current compiler.
 * First, we look for a matching variable in the enclosing function. If we find one, we capture that local and return. This is the base case.
 * Otherwise, we look for a local variable beyond the immediately enclosing function. We do this by recursively calling resolveUpvalue()
 * on the enclosing compiler, not the current one. This series of resolveUpvalue calls works its way along the chain of
 * nested compilers until it hits one of the base cases - either it finds an actual local variable to capture or it runs
 * out of compilers.
 * When the local variable is found, the most deeply nested call to resolveUpvalue captures it and returns the upvalue index.
 * That returns to the next call for the inner function declaration, that call captures the upvalue from the surrounding
 * function and so on. As each nested call to resolveUpvalue returns, we drill back down to the innermost function declaration
 * where the identifier we are resolving appears. At each step along the way, we add an upvalue to the intervening function
 * and pass the resulting upvalue index down to the next call.
 * @param compiler
 * @param name
 * @return
 */
static int resolveUpvalue(Compiler* compiler, Token* name) {
    // Since each compiler stores a pointer to the compiler for the enclosing function, and these pointers form a linked chain
    // that goes all the way to the root compiler for the top-level code. Thus, if the enclosing compiler is NULL, we know we've
    // reached the outermost function without finding a local variable. The variable must be global, so we return -1.
    if (compiler->enclosing == NULL) return -1;

    // Try resolving the variable in the enclosing function.
    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        // if we end up creating an upvalue for a local variable, we mark it as captured.
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t) local, true);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t) upvalue, false);
    }

    return -1;
}

/**
 * Function to initialize the next available local in the compiler's array of variables.
 * Stores the variable's name and the depth of the scope that owns the variable.
 * @param name The token representing the variable.
 */
static void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function (CTok supports upto 256 variables in a block).");
        return;
    }
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
}

/**
 * Function to record the existence of a local variable. Adds the current variable being parsed to the compiler's list
 * of variables in the current scope.
 */
static void declareVariable() {
    // Only performed for local variables, so if the compiler is in top-level global scope - it just bails out.
    // Since global variables are late bound, the compiler doesn't keep track of which declarations for them it has seen.
    if (current->scopeDepth == 0) return;

    Token* name = &parser.previous;

    /**
     * Error handling code:
     * Local variables are appended to the array when they're declared, which means the current scope is always at the
     * end of the array, When we declare a new variable, we start at the end and work our way backward, looking for an
     * existing variable with the same name. If we find one in the current scope, we report the error. Otherwise,
     * if we reach the beginning of the array or a variable owned by another scope, we know that we've checked all of
     * the existing variables in the scope.
     */
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        if (identifiersEqual(name, &local->name)) {
            error("Already variable with this name in this scope.");
        }
    }

    addLocal(*name);
}

/**
 *  Parses a variable identifier, stores its name as an ObjString* in the constant table.
 * @param errorMessage error message to be displayed when the next token isn't a TOKEN_IDENTIFIER
 * @return returns the index of the identifier in the constant table.
 */
static uint8_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    /**
     * First, the variable is 'declared' using declareVariable();
     * We exit the function if we're in local scope since, at runtime, locals aren't looked up
     * by name, so, there's no need to stuff the variable's name into the constant table.
     * So, we return a dummy table index instead.
     */
    declareVariable();
    if (current->scopeDepth > 0) return 0;

    // returns the index of the identifier string (the variable name) in the constant table.
    return identifierConstant(&parser.previous);
}

/**
 * Sets the scope depth of the local in the compiler. This marks the point when the variable is finally 'defined'.
 */
static void markInitialized() {
    // A top level function declaration can call this, when that happens, there is no local variable to mark as initialized.
    // Since the function is bound to a global variable. Hence, we simply return.
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

/**
 * Outputs the bytecode instruction to define a new variable and store its initial value in the global constant table.
 * The index of the variable's name in the global constant table is the instruction's operand.
 * @param global index of the variable's name in the chunk's constant table.
 */
static void defineVariable(uint8_t global) {
    /**
    * In case we're in local scope, we don't need to emit any bytecode, since locals aren't created at runtime.
     * The VM has already executed the code for the variable's initializer (or the implicit nil if the user omitted the initializer),
     * and that value is sitting on top of the stack as the only remaining temporary.
     * We also know that new locals are allocated at the top of the stack....right where the value already is.
     * Thus, there's nothing to do. The temporary simply becomes the local variable.
    */
    if (current->scopeDepth > 0) {
        // once the variable's initializer has been compiled, we mark it initialized.
        markInitialized();
        return;
    }
    emitBytes(OP_DEFINE_GLOBAL, global);
}

/**
 * Function to parse an argument list of a function call.
 * We keep track of the number of arguments we've encountered in <code>argCount</code> and keep parsing the arguments
 * separated by commas with a call to <code>expression()</code>.
 * @return number of arguments encountered in the parsed function call.
 */
static uint8_t argumentList() {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            // Since our 8 bit operands cannot represent more than 255.
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

/**
 * Function to parse the 'and' logical operator.
 * At the point this function is called, the left-hand side expression has already been compiled.
 * That means at runtime, its value will be on top of the stack.
 * If that value is falsey, then we know the entire and must be false, so we skip the right operand and leave the left-hand
 * side value as teh result of the entire expression.
 * Otherwise, we discard teh left-hand value and evaluate the right operand which becomes the result of the whole 'and' operation.
 * @param canAssign
 */
static void and_(bool canAssign) {
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
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
 * Parses blocks of statements.
 */
static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

/**
 * Compiles a function - its parameter list and block body.
 * Generates code that leaves the function on top of the stack.
 * We create a separate compiler for each function being compiled. When we start compiling a function declaration,
 * we create a new Compiler on the C stack and initialize it. <code>initCompiler()</code> sets that to be the current one.
 * Then, as we compile the body, all of the functions that emit bytecode write to the chunk owned by the new compiler's function.
 * After we reach the end of the function we call <code>endCompiler()</code>. That yields the newly compiled function object,
 * which we store as a constant in the surrounding function's constant table. We get a reference back to the surrounding
 * function using the linked list structure in our compiler.
 * @param type the kind of function being compiled.
 */
static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);
    // The beginScope() doesn't have a corresponding endScope() call. Because we end Compiler completely when we reach
    // the end of the function body.
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

    // In case the parameter list is not empty, parse the parameters.
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            // arity gets incremented with each parameter we encounter.
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            // Semantically, a parameter is simply a local variable declared in the outermost lexical scope of the function body.
            // These variables get initialized later when we pass arguments into function calls.
            uint8_t constant = parseVariable("Expect parameter name.");
            defineVariable(constant);
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction* function = endCompiler();
    // We emit an OP_CLOSURE instruction which takes a single operand that represents a constant table index for the function.
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

    // For each upvalue the closure captures, there are two single-byte operands. Each pair of operands specifies what that
    // upvalue captures. If the first byte is 1, it captures a local variable in the enclosing function.
    // If 0, it captures one of the enclosing function's upvalues.
    // The next byte is the local slot or the upvalue index to capture.
    for (int i = 0; i < function->upvalueCount; ++i) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

/**
 * Parses a class declaration.
 */
static void classDeclaration() {
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    // We take the identifier (class name) and add it to the surrounding function's constant table as a string.
    // Compiler needs to stuff the name somewhere the runtime can find it, the constant table is the way to do that.
    uint8_t nameConstant = identifierConstant(&parser.previous);
    // The class's name is also used to bind the class object to a variable of the same name. So we declare a variable
    // of the same name. So we declare a variable with that identifier right after consuming its token.
    declareVariable();
    // emit an instruction to create the class object at runtime. The instruction takes the constant table index of the
    // class's name as an operand.
    emitBytes(OP_CLASS, nameConstant);
    // we define the variable before we parse the body. That way users can refer to the containing class inside the bodies
    // of its own methods. That's useful for factory methods that the user may want to define.
    defineVariable(nameConstant);

    consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
}

/**
 * Parses function declaration.
 * Functions are first-class values, and a function declaration simply creates adn stores one ina  newly declared variable.
 * So we parse the name just like any other variable declaration. A function declaration at the top level will bind the
 * function to a global variable. Inside a block or other function, a function declaration creates its own local variable.
 */
static void funDeclaration() {
    uint8_t global = parseVariable("Expect function name.");
    // To support recursion, we mark a function declaration as initialized, before completely parsing it. Unlike in case of local variables.
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
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
 * Function to compile 'for' loops.
 *
 */
static void forStatement() {
    // In case of variable declarations, we only want them to be scoped to the for loop.
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    // initializer clause.
    if (match(TOKEN_SEMICOLON)) {
        // No initializer.
    } else if (match(TOKEN_VAR)) {
        // if the initializer consist of a variable declaration.
        varDeclaration();
    } else {
        // if the initializer is an expression statement. We call expressionStatement() instead of expression().
        // This is because it consumes the semicolon, and also emits an OP_POP instruction to discard the value from the stack.
        // We don't want initializer leaving anything on the stack.
        expressionStatement();
    }

    int loopStart = currentChunk()->count;

    // Condition clause
    int exitJump = -1;
    // if a condition is present.
    if (!match(TOKEN_SEMICOLON)) {
        // parse the expression for the condition.
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        // Jump out of the loop if the condition is false.
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP); // Condition. Ensures we discard the value when the condition is true.
    }

    // Increment clause.
    if (!match(TOKEN_RIGHT_PAREN)) {
        // if increment clause is present.
        // The increment clause textually appears before the body, but executes after it. So we jump over the increment,
        // run the body, jump back up to the increment, run int, then go to the next iteration.
        int bodyJump = emitJump(
                OP_JUMP); // unconditional jump to hop over the increment clause to the body of the loop the first time.
        int incrementStart = currentChunk()->count;
        // compile the increment expression itself (usually an assignment).
        expression();
        // pop the expression's value from the stack, we were only interested in the side effect, not the actual value produced.
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        // loop instruction to take us back to the top of the 'for' loop - right before teh condition expression (if there is one).
        // That loop happens right after the increment, since the increment happens at the end of each loop iteration.
        emitLoop(loopStart);
        // Then we change the loopStart to point to the offset where the increment expression begins.
        // Later when we emit the loop instruction after the bofy statement, this will cause it to jump up to the increment expression
        // instead of the loop like it does when there is no increment.
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    statement();
    emitLoop(loopStart);

    // If there is a condition clause, this is where we jump to once the condition becomes false.
    if (exitJump != -1) {
        patchJump(exitJump);
        // pop the condition expression's value from the stack.
        emitByte(OP_POP);
    }
    endScope();
}

/**
 * Parses an 'if' statement.
 * Compiles a condition expression, then emits an OP_JUMP_IF_FALSE instruction.
 * To know how far we need to jump, we use a trick called backpatching.
 * We emit the jump instruction first with a placeholder offset operand. We keep track of where that half-finished instruction is.
 * Next, we compile the then body. Once that’s done, we know how far to jump.
 * So we go back and replace that placeholder offset with the real one now that we can calculate it.
 *
 * The implementation ensures that every if statement has an implicit else branch even if teh user didn't write an else clause.
 * In case they left it off, all the branch does is discard the condition value.
 *
 */
static void ifStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    // Compile the condition expression. At runtime we leave the condition value on top of the stack.
    // We'll use that to determine whether to execute the 'then' branch or skip it.
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    // We emit an OP_JUMP_IF_FALSE instruction. It has an operand for how much to offset the ip - how many bytes of code to skip.
    // If the condition is falsey, it adjusts the ip by that amount.
    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    // Jump instruction in case the if condition was true - we will need to jump over the else block.
    // Unlike the OP_JUMP_IF_FALSE, this jump is unconditional, and always runs.
    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    // Check if there's an else block after the if block.
    if (match(TOKEN_ELSE)) statement();
    patchJump(elseJump);
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
 * Parses a return statement. Compiler reaches here after reading TOKEN_RETURN.
 */
static void returnStatement() {
    // We can't use a return statement inside top-level code.
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
    }

    // In case no value is returned, emit an implicit NIL value by calling emitReturn().
    if (match(TOKEN_SEMICOLON)) {
        emitReturn();
    } else {
        // In case a value is returned, parse that value and emit the OP_RETURN.
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

/**
 * Function to compile 'while' statement.
 * Similar to the 'if' handler, we compile the condition expression, surrounded by the mandatory parantheses.
 * That's followed by a jump instruction that skips over the subsequent body statement if the condition is falsey.
 * We patch the jump after compiling the body and take care to pop the condition value from the stack on either path.
 * After the body, we call emitLoop to emit a 'loop' instruction. That instruction needs to know how far back to jump,
 * we already recorded that at the start of the function in loopStart.
 * After executing the body of the while loop, we jump all the way back to before the condition. That way, we re-evaluate
 * the condition expression on each iteration. We store the chunk's current instruction count in loopStart to record the offset
 * in the bytecode right before the condition expression we're about to compile, and then pass it to emitLoop.
 */
static void whileStatement() {
    // point where want to loop back
    int loopStart = currentChunk()->count;

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    // consume the condition expression.
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    // Emit jump statement in case you need to skip over the body.
    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();
    // Emit the looping instruction.
    emitLoop(loopStart);

    patchJump(exitJump);
    // pop the condition value from the stack in case you jumped over the body.
    emitByte(OP_POP);
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
    if (match(TOKEN_CLASS)) {
        classDeclaration();
    } else if (match(TOKEN_FUN)) {
        funDeclaration();
    } else if (match(TOKEN_VAR)) {
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
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
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
ObjFunction* compile(const char* source) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    parser.hadError = false;
    parser.panicMode = false;

    // primes the compiler, by reading the first token.
    advance();
    while (!match(TOKEN_EOF)) {
        declaration();
    }

    // Our compiler returns a reference to the function it just compiled.
    ObjFunction* function = endCompiler();
    // We return the function object if the code compiled properly, otherwise we return NULL.
    // This makes sure the VM doesn't try to execute a function that may contain invalid bytecode.
    return parser.hadError ? NULL : function;
}

/**
 * Collection can begin during any allocation. Those allocations don’t just happen while the user’s program is running.
 * The compiler itself periodically grabs memory from the heap for literals and the constant table.
 * If the GC runs while we’re in the middle of compiling, then any values the compiler directly accesses need to be treated as roots too.
 */
void markCompilerRoots() {
    Compiler* compiler = current;
    while (compiler != NULL) {
        markObject((Obj*) compiler->function);
        compiler = compiler->enclosing;
    }
}