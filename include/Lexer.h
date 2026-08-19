#ifndef LEXER_H
#define LEXER_H

#include <string>

// Token definitions
enum Token {
    tok_eof = -1,
    tok_def = -2,
    tok_extern = -3,
    tok_identifier = -4,
    tok_number = -5,
    tok_if = -6,
    tok_then = -7,
    tok_else = -8,
    tok_for = -9,
    tok_in = -10,
    tok_binary = -11,
    tok_unary = -12,
    tok_var = -13,
    tok_print = -14,
    tok_string = -15,
    tok_input = -16,
    tok_asm = -17,
    tok_asm_intel = -18,
    tok_asm_att =  -19,
    tok_end_asm = -20,
    tok_star = -21,
    tok_ampersand = -22,
    tok_null = -23,
    tok_let = -24

};

// Global lexer state
extern std::string IdentifierStr;
extern double NumVal;
extern std::string StringVal;

// Lexer function
int gettok();

#endif