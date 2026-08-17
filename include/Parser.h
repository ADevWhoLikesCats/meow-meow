#ifndef PARSER_H
#define PARSER_H

#include "AST.h"
#include "Lexer.h"
#include <map>
#include <memory>

// Parser state
extern int CurTok;
extern std::map<char, int> BinopPrecedence;

// Helper functions
int getNextToken();
int GetTokPrecedence();

// Logging helpers
std::unique_ptr<ExprAST> LogError(const char *Str);
std::unique_ptr<PrototypeAST> LogErrorP(const char *Str);
llvm::Value *LogErrorV(const char *Str);

// Parser functions
std::unique_ptr<ExprAST> ParseExpression();
std::unique_ptr<PrototypeAST> ParsePrototype();
std::unique_ptr<FunctionAST> ParseDefinition();
std::unique_ptr<FunctionAST> ParseTopLevelExpr();
std::unique_ptr<PrototypeAST> ParseExtern();

// Parse handlers for expressions
std::unique_ptr<ExprAST> ParseNumberExpr();
std::unique_ptr<ExprAST> ParseStringExpr();
std::unique_ptr<ExprAST> ParsePrintExpr();
std::unique_ptr<ExprAST> ParseInputExpr();
std::unique_ptr<ExprAST> ParseAsmExpr();
std::unique_ptr<ExprAST> ParseParenExpr();
std::unique_ptr<ExprAST> ParseIdentifierExpr();
std::unique_ptr<ExprAST> ParseIfExpr();
std::unique_ptr<ExprAST> ParseForExpr();
std::unique_ptr<ExprAST> ParseVarExpr();
std::unique_ptr<ExprAST> ParsePrimary();
std::unique_ptr<ExprAST> ParseUnary();
std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS);

#endif