#include "Parser.h"
#include "CodeGen.h"
#include <map>
#include <memory>
#include <cstdio>
#include <cctype>

// Define parser globals
int CurTok;

int getNextToken() {
    return CurTok = gettok();
}

int GetTokPrecedence() {
    if (!isascii(CurTok)) return -1;
    int TokPrec = BinopPrecedence[CurTok];
    if (TokPrec <= 0) return -1;
    return TokPrec;
}

std::unique_ptr<ExprAST> LogError(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
    LogError(Str);
    return nullptr;
}

llvm::Value *LogErrorV(const char *Str) {
    LogError(Str);
    return nullptr;
}

// ==================== PARSER IMPLEMENTATIONS ====================

/// numberexpr ::= number
std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken();
    return std::move(Result);
}

/// stringexpr ::= "string"
std::unique_ptr<ExprAST> ParseStringExpr() {
    auto Result = std::make_unique<StringExprAST>(StringVal);
    getNextToken();
    return std::move(Result);
}

/// printexpr ::= 'print' '(' expression ')'
std::unique_ptr<ExprAST> ParsePrintExpr() {
    getNextToken(); // eat 'print'

    if (CurTok != '(')
        return LogError("expected '(' after print");

    getNextToken(); // eat '('

    if (CurTok == tok_string) {
        std::string Str = StringVal;
        getNextToken();
        if (CurTok != ')')
            return LogError("expected ')' after print argument");
        getNextToken();

        std::vector<std::unique_ptr<ExprAST>> Args;
        auto StrExpr = std::make_unique<StringExprAST>(Str);
        Args.push_back(std::move(StrExpr));
        return std::make_unique<CallExprAST>("printstr", std::move(Args));
    }

    auto Arg = ParseExpression();
    if (!Arg) return nullptr;

    if (CurTok != ')')
        return LogError("expected ')' after print argument");

    getNextToken();

    std::vector<std::unique_ptr<ExprAST>> Args;
    Args.push_back(std::move(Arg));
    return std::make_unique<CallExprAST>("printd", std::move(Args));
}

/// inputexpr ::= 'input' '(' ')'
std::unique_ptr<ExprAST> ParseInputExpr() {
    getNextToken(); // eat 'input'

    if (CurTok != '(')
        return LogError("expected '(' after input");

    getNextToken(); // eat '('

    if (CurTok != ')')
        return LogError("expected ')' after input");

    getNextToken(); // eat ')'

    return std::make_unique<CallExprAST>("inputd", std::vector<std::unique_ptr<ExprAST>>());
}

/// asmexpr ::= 'asm' '(' string ')'
std::unique_ptr<ExprAST> ParseAsmExpr() {
    getNextToken(); // eat 'asm'

    if (CurTok != '(')
        return LogError("expected '(' after asm");

    getNextToken(); // eat '('

    if (CurTok != tok_string)
        return LogError("expected assembly string after asm");

    std::string AsmCode = StringVal;
    getNextToken(); // eat the string

    if (CurTok != ')')
        return LogError("expected ')' after asm string");

    getNextToken(); // eat ')'

    return std::make_unique<AsmExprAST>(AsmCode);
}

/// parenexpr ::= '(' expression ')'
std::unique_ptr<ExprAST> ParseParenExpr() {
    getNextToken(); // eat '('
    auto V = ParseExpression();
    if (!V) return nullptr;

    if (CurTok != ')')
        return LogError("expected ')'");
    getNextToken(); // eat ')'
    return V;
}

/// identifierexpr ::= identifier | identifier '(' expression* ')'
std::unique_ptr<ExprAST> ParseIdentifierExpr() {
    std::string IdName = IdentifierStr;

    getNextToken(); // eat identifier

    if (CurTok != '(') // Simple variable ref
        return std::make_unique<VariableExprAST>(IdName);

    // Function call
    getNextToken(); // eat '('
    std::vector<std::unique_ptr<ExprAST>> Args;
    if (CurTok != ')') {
        while (true) {
            if (auto Arg = ParseExpression())
                Args.push_back(std::move(Arg));
            else
                return nullptr;

            if (CurTok == ')')
                break;

            if (CurTok != ',')
                return LogError("Expected ')' or ',' in argument list");
            getNextToken();
        }
    }

    getNextToken(); // eat ')'
    return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

/// ifexpr ::= 'if' expression 'then' expression 'else' expression
std::unique_ptr<ExprAST> ParseIfExpr() {
    getNextToken(); // eat 'if'

    auto Cond = ParseExpression();
    if (!Cond) return nullptr;

    if (CurTok != tok_then)
        return LogError("expected then");
    getNextToken(); // eat 'then'

    auto Then = ParseExpression();
    if (!Then) return nullptr;

    if (CurTok != tok_else)
        return LogError("expected else");
    getNextToken(); // eat 'else'

    auto Else = ParseExpression();
    if (!Else) return nullptr;

    return std::make_unique<IfExprAST>(std::move(Cond), std::move(Then), std::move(Else));
}

/// forexpr ::= 'for' identifier '=' expr ',' expr (',' expr)? 'in' expression
std::unique_ptr<ExprAST> ParseForExpr() {
    getNextToken(); // eat 'for'

    if (CurTok != tok_identifier)
        return LogError("expected identifier after for");

    std::string IdName = IdentifierStr;
    getNextToken(); // eat identifier

    if (CurTok != '=')
        return LogError("expected '=' after for");
    getNextToken(); // eat '='

    auto Start = ParseExpression();
    if (!Start) return nullptr;
    if (CurTok != ',')
        return LogError("expected ',' after for start value");
    getNextToken();

    auto End = ParseExpression();
    if (!End) return nullptr;

    std::unique_ptr<ExprAST> Step;
    if (CurTok == ',') {
        getNextToken();
        Step = ParseExpression();
        if (!Step) return nullptr;
    }

    if (CurTok != tok_in)
        return LogError("expected 'in' after for");
    getNextToken(); // eat 'in'

    auto Body = ParseExpression();
    if (!Body) return nullptr;

    return std::make_unique<ForExprAST>(IdName, std::move(Start), std::move(End),
                                        std::move(Step), std::move(Body));
}

/// varexpr ::= 'var' identifier ('=' expression)? (',' identifier ('=' expression)?)* 'in' expression
std::unique_ptr<ExprAST> ParseVarExpr() {
    getNextToken(); // eat 'var'

    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;

    if (CurTok != tok_identifier)
        return LogError("expected identifier after var");

    while (true) {
        std::string Name = IdentifierStr;
        getNextToken(); // eat identifier

        std::unique_ptr<ExprAST> Init = nullptr;
        if (CurTok == '=') {
            getNextToken(); // eat '='
            Init = ParseExpression();
            if (!Init) return nullptr;
        }

        VarNames.push_back(std::make_pair(Name, std::move(Init)));

        if (CurTok != ',')
            break;
        getNextToken(); // eat ','

        if (CurTok != tok_identifier)
            return LogError("expected identifier list after var");
    }

    if (CurTok != tok_in)
        return LogError("expected 'in' keyword after 'var'");
    getNextToken(); // eat 'in'

    auto Body = ParseExpression();
    if (!Body) return nullptr;

    return std::make_unique<VarExprAST>(std::move(VarNames), std::move(Body));
}

/// letexpr ::= 'let' identifier '=' expression
std::unique_ptr<ExprAST> ParseLetExpr() {
    getNextToken(); // eat 'let'

    if (CurTok != tok_identifier)
        return LogError("expected identifier after let");

    std::string Name = IdentifierStr;
    getNextToken(); // eat identifier

    if (CurTok != '=')
        return LogError("expected '=' after identifier");
    getNextToken(); // eat '='

    auto Init = ParseExpression();
    if (!Init) return nullptr;

    return std::make_unique<LetExprAST>(Name, std::move(Init));
}




    

std::unique_ptr<ExprAST> ParseAsmBlock() {
    bool UseIntelSyntax;

    if (CurTok == tok_asm_intel) {
        UseIntelSyntax = true;
        getNextToken();
    } else if (CurTok == tok_asm_att) {
        UseIntelSyntax = false;
        getNextToken();
    } else {
        return LogError("Expected ASM_INTEL or ASM_ATT");
    }

    std::vector<std::string> Instructions;

    // Read instructions until END_ASM
    while (CurTok != tok_end_asm && CurTok != tok_eof) {
        // Skip newlines and whitespace
        if (CurTok == ';' || CurTok == '\n') {
            getNextToken();
            continue;
        }

        // Read the entire line as a string
        std::string Line;
        while (CurTok != tok_end_asm && CurTok != tok_eof && CurTok != ';' && CurTok != '\n') {
            if (CurTok == tok_identifier) {
                Line += IdentifierStr;
            } else if (CurTok == tok_number) {
                Line += std::to_string(NumVal);
            } else if (CurTok == tok_string) {
                Line += "\"" + StringVal + "\"";
            } else {
                // For other tokens (operators, punctuation), add them as characters
                Line += (char)CurTok;
            }
            getNextToken();
        }

        // Trim the line
        if (!Line.empty()) {
            Instructions.push_back(Line);
        }

        // Skip to the next line if we hit a semicolon
        if (CurTok == ';') {
            getNextToken();
        }
    }

    if (CurTok != tok_end_asm)
        return LogError("Expected END_ASM");
    getNextToken();

    return std::make_unique<AsmBlockExprAST>(Instructions, UseIntelSyntax);
}

/// addressofexpr ::= '&' expression
std::unique_ptr<ExprAST> ParseAddressOf() {
    getNextToken(); // eat '&'
    auto Operand = ParseExpression();
    if (!Operand) return nullptr;
    return std::make_unique<AddressOfExprAST>(std::move(Operand));
}

/// derefexpr ::= '*' expression
std::unique_ptr<ExprAST> ParseDeref() {
    getNextToken(); // eat '*'
    auto Operand = ParseExpression();
    if (!Operand) return nullptr;
    return std::make_unique<DerefExprAST>(std::move(Operand));
}

/// nullexpr ::= 'null'
std::unique_ptr<ExprAST> ParseNull() {
    getNextToken(); // eat 'null'
    return std::make_unique<NullExprAST>();
}


/// primary ::= identifierexpr | numberexpr | parenexpr | ifexpr | forexpr | varexpr | print | input | asm
std::unique_ptr<ExprAST> ParsePrimary() {
    switch (CurTok) {
    default:
        return LogError("unknown token when expecting an expression");
    case tok_identifier:
        return ParseIdentifierExpr();
    case tok_number:
        return ParseNumberExpr();
    case '(':
        return ParseParenExpr();
    case tok_if:
        return ParseIfExpr();
    case tok_for:
        return ParseForExpr();
    case tok_var:
        return ParseVarExpr();
    case tok_print:
        return ParsePrintExpr();
    case tok_input:
        return ParseInputExpr();
    case tok_asm:
        return ParseAsmExpr();
    case tok_asm_intel:
    case tok_asm_att:
        return ParseAsmBlock();
    case tok_null:
        return ParseNull();
    case tok_ampersand:
        return ParseAddressOf();
    case tok_star:
        return ParseDeref();
    case tok_let:
        return ParseLetExpr();            
    }
}

/// unary ::= primary | '!' unary
std::unique_ptr<ExprAST> ParseUnary() {
    // Handle address-of operator
    if (CurTok == tok_ampersand) {
        getNextToken(); // eat '&'
        auto Operand = ParseUnary(); // Parse the operand (highest precedence)
        if (!Operand) return nullptr;
        return std::make_unique<AddressOfExprAST>(std::move(Operand));
    }

    // Handle dereference operator
    if (CurTok == tok_star) {
        getNextToken(); // eat '*'
        auto Operand = ParseUnary(); // Parse the operand (highest precedence)
        if (!Operand) return nullptr;
        return std::make_unique<DerefExprAST>(std::move(Operand));
    }

    // If not a unary operator, parse primary
    if (!isascii(CurTok) || CurTok == '(' || CurTok == ',')
        return ParsePrimary();

    // Handle other unary operators (like '!')
    int Opc = CurTok;
    getNextToken();
    if (auto Operand = ParseUnary())
        return std::make_unique<UnaryExprAST>(Opc, std::move(Operand));
    return nullptr;
}

/// binoprhs ::= ('+' unary)*
std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS) {
    while (true) {
        int TokPrec = GetTokPrecedence();

        if (TokPrec < ExprPrec)
            return LHS;

        int BinOp = CurTok;
        getNextToken(); // eat binop

        auto RHS = ParseUnary();
        if (!RHS) return nullptr;

        int NextPrec = GetTokPrecedence();
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
            if (!RHS) return nullptr;
        }

        LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
    }
}

/// expression ::= unary binoprhs
std::unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParseUnary();
    if (!LHS) return nullptr;
    return ParseBinOpRHS(0, std::move(LHS));
}

/// prototype ::= id '(' id* ')' | binary LETTER number? (id, id) | unary LETTER (id)
std::unique_ptr<PrototypeAST> ParsePrototype() {
    std::string FnName;
    unsigned Kind = 0;
    unsigned BinaryPrecedence = 30;

    switch (CurTok) {
    default:
        return LogErrorP("Expected function name in prototype");
    case tok_identifier:
        FnName = IdentifierStr;
        Kind = 0;
        getNextToken();
        break;
    case tok_unary:
        getNextToken();
        if (!isascii(CurTok))
            return LogErrorP("Expected unary operator");
        FnName = "unary";
        FnName += (char)CurTok;
        Kind = 1;
        getNextToken();
        break;
    case tok_binary:
        getNextToken();
        if (!isascii(CurTok))
            return LogErrorP("Expected binary operator");
        FnName = "binary";
        FnName += (char)CurTok;
        Kind = 2;
        getNextToken();

        if (CurTok == tok_number) {
            if (NumVal < 1 || NumVal > 100)
                return LogErrorP("Invalid precedence: must be 1..100");
            BinaryPrecedence = (unsigned)NumVal;
            getNextToken();
        }
        break;
    }

    if (CurTok != '(')
        return LogErrorP("Expected '(' in prototype");

    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier)
        ArgNames.push_back(IdentifierStr);
    if (CurTok != ')')
        return LogErrorP("Expected ')' in prototype");

    getNextToken(); // eat ')'

    if (Kind && ArgNames.size() != Kind)
        return LogErrorP("Invalid number of operands for operator");

    return std::make_unique<PrototypeAST>(FnName, ArgNames, Kind != 0, BinaryPrecedence);
}

/// definition ::= 'def' prototype expression
std::unique_ptr<FunctionAST> ParseDefinition() {
    fprintf(stderr, "DEBUG: ParseDefinition() called\n");
    getNextToken(); // eat 'def'
    auto Proto = ParsePrototype();
    if (!Proto) return nullptr;

    if (auto E = ParseExpression())
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    return nullptr;
}

/// toplevelexpr ::= expression
std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    if (auto E = ParseExpression()) {
        auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

/// external ::= 'extern' prototype
std::unique_ptr<PrototypeAST> ParseExtern() {
    getNextToken(); // eat 'extern'
    return ParsePrototype();
}