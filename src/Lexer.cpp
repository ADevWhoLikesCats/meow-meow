#include "Lexer.h"
#include <cstdio>
#include <cctype>
#include <string>
#include <iostream>  // For std::cout, std::endl

// Define globals
std::string IdentifierStr;
double NumVal;
std::string StringVal;

int gettok() {
    static int LastChar = ' ';
    
    std::cout << "DEBUG: gettok() called, LastChar = " << LastChar << std::endl;

    // Skip whitespace
    while (isspace(LastChar)) {
        LastChar = std::cin.get();
        std::cout << "DEBUG: Skipping whitespace, LastChar now = " << LastChar << std::endl;
    }

    std::cout << "DEBUG: After skipping whitespace, LastChar = " << LastChar << std::endl;

    // Identifiers and keywords
    if (isalpha(LastChar)) {
        std::cout << "DEBUG: Starting identifier parsing" << std::endl;
        IdentifierStr = LastChar;
        std::cout << "DEBUG: First char: " << IdentifierStr << std::endl;
        
        while (isalnum((LastChar = std::cin.get()))) {
            std::cout << "DEBUG: Added char: " << (char)LastChar << ", IdentifierStr now = " << IdentifierStr << std::endl;
            IdentifierStr += LastChar;
        }
        
        std::cout << "DEBUG: Finished identifier: " << IdentifierStr << std::endl;

        if (IdentifierStr == "def") {
            std::cout << "DEBUG: Found 'def' in lexer, returning tok_def" << std::endl;
            return tok_def;
        }
        if (IdentifierStr == "extern") {
            std::cout << "DEBUG: Found 'extern', returning tok_extern" << std::endl;
            return tok_extern;
        }
        if (IdentifierStr == "if") {
            std::cout << "DEBUG: Found 'if', returning tok_if" << std::endl;
            return tok_if;
        }
        if (IdentifierStr == "then") {
            std::cout << "DEBUG: Found 'then', returning tok_then" << std::endl;
            return tok_then;
        }
        if (IdentifierStr == "else") {
            std::cout << "DEBUG: Found 'else', returning tok_else" << std::endl;
            return tok_else;
        }
        if (IdentifierStr == "for") {
            std::cout << "DEBUG: Found 'for', returning tok_for" << std::endl;
            return tok_for;
        }
        if (IdentifierStr == "in") {
            std::cout << "DEBUG: Found 'in', returning tok_in" << std::endl;
            return tok_in;
        }
        if (IdentifierStr == "binary") {
            std::cout << "DEBUG: Found 'binary', returning tok_binary" << std::endl;
            return tok_binary;
        }
        if (IdentifierStr == "unary") {
            std::cout << "DEBUG: Found 'unary', returning tok_unary" << std::endl;
            return tok_unary;
        }
        if (IdentifierStr == "var") {
            std::cout << "DEBUG: Found 'var', returning tok_var" << std::endl;
            return tok_var;
        }
        if (IdentifierStr == "print") {
            std::cout << "DEBUG: Found 'print', returning tok_print" << std::endl;
            return tok_print;
        }
        if (IdentifierStr == "input") {
            std::cout << "DEBUG: Found 'input', returning tok_input" << std::endl;
            return tok_input;
        }
        if (IdentifierStr == "asm") {
            std::cout << "DEBUG: Found 'asm', returning tok_asm" << std::endl;
            return tok_asm;
        }
        if (IdentifierStr == "ASM_INTEL") {
            std::cout << "DEBUG: Found 'ASM_INTEL', returning tok_asm_intel" << std::endl;
            return tok_asm_intel;
        }
        if (IdentifierStr == "ASM_ATT") {
            std::cout << "DEBUG: Found 'ASM_ATT', returning tok_asm_att" << std::endl;
            return tok_asm_att;
        }
        if (IdentifierStr == "END_ASM") {
            std::cout << "DEBUG: Found 'END_ASM', returning tok_end_asm" << std::endl;
            return tok_end_asm;
        }
        if (IdentifierStr == "null") {
            std::cout << "DEBUG: Found 'null', returning tok_null" << std::endl;
            return tok_null;
        }
        if (IdentifierStr == "let") {
            std::cout << "DEBUG: Found 'let', returning tok_let" << std::endl;
            return tok_let;
        }
        
        std::cout << "DEBUG: Returning tok_identifier: " << IdentifierStr << std::endl;
        return tok_identifier;
    }

    // Numbers
    if (isdigit(LastChar) || LastChar == '.') {
        std::cout << "DEBUG: Starting number parsing" << std::endl;
        std::string NumStr;
        do {
            NumStr += LastChar;
            LastChar = std::cin.get();
        } while (isdigit(LastChar) || LastChar == '.');
        NumVal = strtod(NumStr.c_str(), nullptr);
        std::cout << "DEBUG: Returning tok_number: " << NumVal << std::endl;
        return tok_number;
    }

    // String literals
    if (LastChar == '"') {
        std::cout << "DEBUG: Starting string parsing" << std::endl;
        StringVal = "";
        while ((LastChar = std::cin.get()) != '"' && LastChar != EOF)
            StringVal += LastChar;
        if (LastChar == EOF) {
            std::cout << "DEBUG: EOF reached in string" << std::endl;
            return tok_eof;
        }
        LastChar = std::cin.get();
        std::cout << "DEBUG: Returning tok_string: " << StringVal << std::endl;
        return tok_string;
    }

    // Comments
    if (LastChar == '#') {
        std::cout << "DEBUG: Starting comment" << std::endl;
        do {
            LastChar = std::cin.get();
        } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');
        if (LastChar != EOF) {
            std::cout << "DEBUG: Comment finished, calling gettok() again" << std::endl;
            return gettok();
        }
    }

    // EOF
    if (LastChar == EOF) {
        std::cout << "DEBUG: EOF reached, returning tok_eof" << std::endl;
        return tok_eof;
    }

    // * and & handling
    if (LastChar == '*') {
        std::cout << "DEBUG: Found '*', returning tok_star" << std::endl;
        LastChar = std::cin.get();
        return tok_star;
    }
    if (LastChar == '&') {
        std::cout << "DEBUG: Found '&', returning tok_ampersand" << std::endl;
        LastChar = std::cin.get();
        return tok_ampersand;
    }

    // Otherwise, return the character as its ASCII value
    int ThisChar = LastChar;
    std::cout << "DEBUG: Returning character: " << (char)ThisChar << " (ASCII: " << ThisChar << ")" << std::endl;
    LastChar = std::cin.get();
    return ThisChar;
}