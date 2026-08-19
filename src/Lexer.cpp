#include "Lexer.h"
#include <cstdio>
#include <cctype>
#include <string>

// Define globals
std::string IdentifierStr;
double NumVal;
std::string StringVal;

int gettok() {
    static int LastChar = ' ';

    // Skip whitespace
    while (isspace(LastChar))
        LastChar = getchar();

    // Identifiers and keywords
    if (isalpha(LastChar)) {
        IdentifierStr = LastChar;
        while (isalnum((LastChar = getchar())))
            IdentifierStr += LastChar;

        if (IdentifierStr == "def") {
            fprintf(stderr, "DEBUG: Found 'def' in lexer\n");
            return tok_def;
        }
        if (IdentifierStr == "extern") return tok_extern;
        if (IdentifierStr == "if") return tok_if;
        if (IdentifierStr == "then") return tok_then;
        if (IdentifierStr == "else") return tok_else;
        if (IdentifierStr == "for") return tok_for;
        if (IdentifierStr == "in") return tok_in;
        if (IdentifierStr == "binary") return tok_binary;
        if (IdentifierStr == "unary") return tok_unary;
        if (IdentifierStr == "var") return tok_var;
        if (IdentifierStr == "print") return tok_print;
        if (IdentifierStr == "input") return tok_input;
        if (IdentifierStr == "asm") return tok_asm;
        if (IdentifierStr == "ASM_INTEL") return tok_asm_intel;
        if (IdentifierStr == "ASM_ATT") return tok_asm_att;
        if (IdentifierStr == "END_ASM") return tok_end_asm;
        if (IdentifierStr == "null") return tok_null;
        if (IdentifierStr == "let") return tok_let;
        return tok_identifier;
    }

    // Numbers
    if (isdigit(LastChar) || LastChar == '.') {
        std::string NumStr;
        do {
            NumStr += LastChar;
            LastChar = getchar();
        } while (isdigit(LastChar) || LastChar == '.');
        NumVal = strtod(NumStr.c_str(), nullptr);
        return tok_number;
    }

    // String literals
    if (LastChar == '"') {
        StringVal = "";
        while ((LastChar = getchar()) != '"' && LastChar != EOF)
            StringVal += LastChar;
        if (LastChar == EOF) return tok_eof;
        LastChar = getchar();
        return tok_string;
    }

    // Comments
    if (LastChar == '#') {
        do {
            LastChar = getchar();
        } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');
        if (LastChar != EOF) return gettok();
    }

    // EOF
    if (LastChar == EOF) return tok_eof;

    // * and & handling
    if (LastChar == '*') {
        LastChar = getchar();
        return tok_star;
    }
    if (LastChar == '&') {
        LastChar = getchar();
        return tok_ampersand;
    }

    // Otherwise, return the character as its ASCII value
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
}