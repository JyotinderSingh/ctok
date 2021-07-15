# ctok

```
// Your first Tok Program
print "Hello, Tok!";
```

This repository is an effort to build a fast and performant interpreter for a programming language called **Tok** *(
pronounced 'talk')*.

ctok *(pronounced 'sea-talk')* is an interpreter that internally uses a compiler, and a Bytecode Virtual Machine written
in C for Tok.

This is a much faster and sophisticated implementation compared to
[jtok, the interpreter I wrote for Tok in Java](https://github.com/JyotinderSingh/jtok).

Head over to the [Documentation](/DOCUMENTATION.md) to see code examples and other language features!

## Why on earth did I spend more than 3 months making this?

Honestly, just because I never took a compilers course in uni it felt like I was missing out on some essential
knowledge. Compilers and programming languages have always felt magical to me - and that went behind them has always
seemed like a dark arcane art, only wielded by wizards. In simple words, I wanted to see what was behind this curtain of
magic- and turns out, it's just a few thousand lines of code. Nothing else.

## Under the hood

```
                   ctok architecture
                
            ------------
           |  Compiler  |       <- Front End
            ------------
                  v
            ------------
           |  Bytecode  |       <- Representation
            ------------
                  v
          -----------------
         | Virtual Machine |    <- Execution
          -----------------
```

## The Tok Language Spec

The Tok grammar looks something like this, in order of associativity and precedence:

```
program        → statement* EOF ;

declaration    → classDecl
                 | funDecl
                 | varDecl
                 | statement ;

classDecl      → "class" IDENTIFIER ( "<" IDENTIFIER )?
                 "{" function* "}" ;             
                 
funDecl        → "fun" function ;

function       → IDENTIFIER "(" parameters? ")" block ;             

parameters     → IDENTIFIER ( "," IDENTIFIER )* ;
                 
varDecl        → "var" IDENTIFIER ( "=" expression )? ";" ;

statement      → exprStmt
                 | forStmt
                 | ifStmt
                 | printStmt
                 | returnStmt
                 | whileStmt
                 | block ;

returnStmt     → "return" expression? ";" ;

forStmt        → "for" "(" ( varDecl | exprStmt | ";" )
                 expression? ";"
                 expression? ")" statement ;

whileStmt      → "while" "(" expression ")" statement ;

ifStmt         → "if" "(" expression ")" statement
                 ( "else" statement )? ;

block          → "{" declaration* "}" ;

exprStmt       → expression ";" ;

printStmt      → "print" expression ";" ;

expression     → assignment ;

assignment     → ( call "." )? IDENTIFIER "=" assignment
                 | logic_or ;
               
logic_or       → logic_and ( "or" logic_and )* ;

logic_and      → equality ( "and" equality )* ;

equality       → comparison ( ( "!=" | "==" ) comparison )* ;

comparison     → term ( ( ">" | ">=" | "<" | "<=" ) term )* ;

term           → factor ( ( "-" | "+" ) factor )* ;

factor         → unary ( ( "/" | "*" ) unary )* ;

unary          → ( "!" | "-" ) unary | call ;

call           → primary ( "(" arguments? ")" | "." IDENTIFIER )* ;

arguments      → expression ( "," expression )* ;

primary        → "true" | "false" | "nil" | "this"
               | NUMBER | STRING | IDENTIFIER | "(" expression ")"
               | "super" "." IDENTIFIER ;

```
