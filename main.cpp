#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>

using namespace llvm;
using namespace llvm::sys;

namespace stdfs = std::filesystem;

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
  tok_eof = -1,

  // commands
  tok_def = -2,
  tok_extern = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,

  // control
  tok_if = -6,
  tok_then = -7,
  tok_else = -8,
  tok_for = -9,
  tok_in = -10,

  // operators
  tok_binary = -11,
  tok_unary = -12,

  // var definition
  tok_var = -13,

  // print definition
  tok_print = -14,

  // string definition
  tok_string = -15,

  // input definition
  tok_input = -16
};
static std::string IdentifierStr; // Filled in if tok_identifier
static double NumVal;
static std::string StringVal;             // Filled in if tok_number

/// gettok - Return the next token from standard input.
static int gettok() {
  fprintf(stderr, "DEBUG: gettok() called\n");  
  static int LastChar = ' ';

  // Skip any whitespace.
  while (isspace(LastChar))
    LastChar = getchar();

  // --- IDENTIFIERS AND KEYWORDS ---
  if (isalpha(LastChar)) {
    IdentifierStr = LastChar;
    while (isalnum((LastChar = getchar())))
      IdentifierStr += LastChar;

    if (IdentifierStr == "def")
      return tok_def;
    if (IdentifierStr == "extern")
      return tok_extern;
    if (IdentifierStr == "if")
      return tok_if;
    if (IdentifierStr == "then")
      return tok_then;
    if (IdentifierStr == "else")
      return tok_else;
    if (IdentifierStr == "for")
      return tok_for;
    if (IdentifierStr == "in")
      return tok_in;
    if (IdentifierStr == "binary")
      return tok_binary;
    if (IdentifierStr == "unary")
      return tok_unary;
    if (IdentifierStr == "var")
      return tok_var;
    if (IdentifierStr == "print")
      return tok_print;
    if (IdentifierStr == "input")
      return tok_input;
    return tok_identifier;
    if (IdentifierStr == "def") {
    fprintf(stderr, "DEBUG: Found 'def' keyword!\n");
    return tok_def;
  }
  }

  // --- NUMBERS ---
  if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');
    NumVal = strtod(NumStr.c_str(), nullptr);
    return tok_number;
  }

  // --- STRING LITERALS ---
  if (LastChar == '"') {
    StringVal = "";
    while ((LastChar = getchar()) != '"' && LastChar != EOF)
      StringVal += LastChar;
    if (LastChar == EOF)
      return tok_eof;
    LastChar = getchar(); // eat closing "
    return tok_string;
  }

  // --- COMMENTS ---
  if (LastChar == '#') {
    do
      LastChar = getchar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');
    if (LastChar != EOF)
      return gettok();
  }

  // --- END OF FILE ---
  if (LastChar == EOF)
    return tok_eof;

  // --- OTHER CHARACTERS (operators, punctuation, etc.) ---
  int ThisChar = LastChar;
  LastChar = getchar();
  return ThisChar;
}
//===----------------------------------------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------------------------------------===//

namespace {

/// ExprAST - Base class for all expression nodes.
class ExprAST {
public:
  virtual ~ExprAST() = default;

  virtual Value *codegen() = 0;
};

/// NumberExprAST - Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}

  Value *codegen() override;
};

/// StringExprAST - Expression class for string literals like "hello".
class StringExprAST : public ExprAST {
  std::string Val;

public:
  StringExprAST(const std::string &Val) : Val(Val) {}

  Value *codegen() override;
};

/// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
  std::string Name;

public:
  VariableExprAST(const std::string &Name) : Name(Name) {}

  Value *codegen() override;
  const std::string &getName() const { return Name; }
};

/// UnaryExprAST - Expression class for a unary operator.
class UnaryExprAST : public ExprAST {
  char Opcode;
  std::unique_ptr<ExprAST> Operand;

public:
  UnaryExprAST(char Opcode, std::unique_ptr<ExprAST> Operand)
      : Opcode(Opcode), Operand(std::move(Operand)) {}

  Value *codegen() override;
};

/// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}

  Value *codegen() override;
};

/// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
  std::string Callee;
  std::vector<std::unique_ptr<ExprAST>> Args;

public:
  CallExprAST(const std::string &Callee,
              std::vector<std::unique_ptr<ExprAST>> Args)
      : Callee(Callee), Args(std::move(Args)) {}

  Value *codegen() override;
};

/// IfExprAST - Expression class for if/then/else.
class IfExprAST : public ExprAST {
  std::unique_ptr<ExprAST> Cond, Then, Else;

public:
  IfExprAST(std::unique_ptr<ExprAST> Cond, std::unique_ptr<ExprAST> Then,
            std::unique_ptr<ExprAST> Else)
      : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}

  Value *codegen() override;
};

/// ForExprAST - Expression class for for/in.
class ForExprAST : public ExprAST {
  std::string VarName;
  std::unique_ptr<ExprAST> Start, End, Step, Body;

public:
  ForExprAST(const std::string &VarName, std::unique_ptr<ExprAST> Start,
             std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step,
             std::unique_ptr<ExprAST> Body)
      : VarName(VarName), Start(std::move(Start)), End(std::move(End)),
        Step(std::move(Step)), Body(std::move(Body)) {}

  Value *codegen() override;
};

/// VarExprAST - Expression class for var/in
class VarExprAST : public ExprAST {
  std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;
  std::unique_ptr<ExprAST> Body;

public:
  VarExprAST(
      std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames,
      std::unique_ptr<ExprAST> Body)
      : VarNames(std::move(VarNames)), Body(std::move(Body)) {}

  Value *codegen() override;
};

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes), as well as if it is an operator.
class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;
  bool IsOperator;
  unsigned Precedence; // Precedence if a binary op.

public:
  PrototypeAST(const std::string &Name, std::vector<std::string> Args,
               bool IsOperator = false, unsigned Prec = 0)
      : Name(Name), Args(std::move(Args)), IsOperator(IsOperator),
        Precedence(Prec) {}

  Function *codegen();
  const std::string &getName() const { return Name; }

  bool isUnaryOp() const { return IsOperator && Args.size() == 1; }
  bool isBinaryOp() const { return IsOperator && Args.size() == 2; }

  char getOperatorName() const {
    assert(isUnaryOp() || isBinaryOp());
    return Name[Name.size() - 1];
  }

  unsigned getBinaryPrecedence() const { return Precedence; }
};

/// FunctionAST - This class represents a function definition itself.
class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}

  Function *codegen();
};

} // end anonymous namespace


//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

/// CurTok/getNextToken - Provide a simple token buffer.  CurTok is the current
/// token the parser is looking at.  getNextToken reads another token from the
/// lexer and updates CurTok with its results.
static int CurTok;
static int getNextToken() { return CurTok = gettok(); }

// Forward declarations
static std::unique_ptr<ExprAST> ParseExpression();
static std::unique_ptr<FunctionAST> ParseTopLevelExpr();  

/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
static std::map<char, int> BinopPrecedence;

/// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  // Make sure it's a declared binop.
  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0)
    return -1;
  return TokPrec;
}

/// LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "Error: %s\n", Str);
  return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  LogError(Str);
  return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken(); // consume the number
  return std::move(Result);
}

/// stringexpr ::= "string"
static std::unique_ptr<ExprAST> ParseStringExpr() {
  auto Result = std::make_unique<StringExprAST>(StringVal);
  getNextToken(); // consume the string
  return std::move(Result);
}

static std::unique_ptr<ExprAST> ParsePrintExpr() {
  getNextToken(); // eat 'print'

  if (CurTok != '(')
    return LogError("expected '(' after print");

  getNextToken(); // eat '('

  // --- Check if it's a string ---
  if (CurTok == tok_string) {
    std::string Str = StringVal;
    getNextToken(); // eat the string
    if (CurTok != ')')
      return LogError("expected ')' after print argument");
    getNextToken(); // eat ')'

    std::vector<std::unique_ptr<ExprAST>> Args;
    auto StrExpr = std::make_unique<StringExprAST>(Str);
    Args.push_back(std::move(StrExpr));
    return std::make_unique<CallExprAST>("printstr", std::move(Args));
  }

  // --- Number printing (existing code) ---
  auto Arg = ParseExpression();
  if (!Arg)
    return nullptr;

  if (CurTok != ')')
    return LogError("expected ')' after print argument");

  getNextToken(); // eat ')'

  std::vector<std::unique_ptr<ExprAST>> Args;
  Args.push_back(std::move(Arg));
  return std::make_unique<CallExprAST>("printd", std::move(Args));
}

/// inputexpr ::= 'input' '(' ')'
static std::unique_ptr<ExprAST> ParseInputExpr() {
  getNextToken(); // eat 'input'

  if (CurTok != '(')
    return LogError("expected '(' after input");

  getNextToken(); // eat '('

  if (CurTok != ')')
    return LogError("expected ')' after input");

  getNextToken(); // eat ')'

  return std::make_unique<CallExprAST>("inputd", std::vector<std::unique_ptr<ExprAST>>());
}

/// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken(); // eat (.
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurTok != ')')
    return LogError("expected ')'");
  getNextToken(); // eat ).
  return V;
}

/// identifierexpr
///   ::= identifier
///   ::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;

  getNextToken(); // eat identifier.

  if (CurTok != '(') // Simple variable ref.
    return std::make_unique<VariableExprAST>(IdName);

  // Call.
  getNextToken(); // eat (
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

  // Eat the ')'.
  getNextToken();

  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

/// ifexpr ::= 'if' expression 'then' expression 'else' expression
static std::unique_ptr<ExprAST> ParseIfExpr() {
  getNextToken(); // eat the if.

  // condition.
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurTok != tok_then)
    return LogError("expected then");
  getNextToken(); // eat the then

  auto Then = ParseExpression();
  if (!Then)
    return nullptr;

  if (CurTok != tok_else)
    return LogError("expected else");

  getNextToken();

  auto Else = ParseExpression();
  if (!Else)
    return nullptr;

  return std::make_unique<IfExprAST>(std::move(Cond), std::move(Then),
                                      std::move(Else));
}

/// forexpr ::= 'for' identifier '=' expr ',' expr (',' expr)? 'in' expression
static std::unique_ptr<ExprAST> ParseForExpr() {
  getNextToken(); // eat the for.

  if (CurTok != tok_identifier)
    return LogError("expected identifier after for");

  std::string IdName = IdentifierStr;
  getNextToken(); // eat identifier.

  if (CurTok != '=')
    return LogError("expected '=' after for");
  getNextToken(); // eat '='.

  auto Start = ParseExpression();
  if (!Start)
    return nullptr;
  if (CurTok != ',')
    return LogError("expected ',' after for start value");
  getNextToken();

  auto End = ParseExpression();
  if (!End)
    return nullptr;

  // The step value is optional.
  std::unique_ptr<ExprAST> Step;
  if (CurTok == ',') {
    getNextToken();
    Step = ParseExpression();
    if (!Step)
      return nullptr;
  }

  if (CurTok != tok_in)
    return LogError("expected 'in' after for");
  getNextToken(); // eat 'in'.

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return std::make_unique<ForExprAST>(IdName, std::move(Start), std::move(End),
                                       std::move(Step), std::move(Body));
}

/// varexpr ::= 'var' identifier ('=' expression)?
//                    (',' identifier ('=' expression)?)* 'in' expression
static std::unique_ptr<ExprAST> ParseVarExpr() {
  getNextToken(); // eat the var.

  std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;

  // At least one variable name is required.
  if (CurTok != tok_identifier)
    return LogError("expected identifier after var");

  while (true) {
    std::string Name = IdentifierStr;
    getNextToken(); // eat identifier.

    // Read the optional initializer.
    std::unique_ptr<ExprAST> Init = nullptr;
    if (CurTok == '=') {
      getNextToken(); // eat the '='.

      Init = ParseExpression();
      if (!Init)
        return nullptr;
    }

    VarNames.push_back(std::make_pair(Name, std::move(Init)));

    // End of var list, exit loop.
    if (CurTok != ',')
      break;
    getNextToken(); // eat the ','.

    if (CurTok != tok_identifier)
      return LogError("expected identifier list after var");
  }

  // At this point, we have to have 'in'.
  if (CurTok != tok_in)
    return LogError("expected 'in' keyword after 'var'");
  getNextToken(); // eat 'in'.

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return std::make_unique<VarExprAST>(std::move(VarNames), std::move(Body));
}

/// primary
///   ::= identifierexpr
///   ::= numberexpr
///   ::= parenexpr
///   ::= ifexpr
///   ::= forexpr
///   ::= varexpr
static std::unique_ptr<ExprAST> ParsePrimary() {
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
    return ParseVarExpr();
  }
}

/// unary
///   ::= primary
///   ::= '!' unary
static std::unique_ptr<ExprAST> ParseUnary() {
  // If the current token is not an operator, it must be a primary expr.
  if (!isascii(CurTok) || CurTok == '(' || CurTok == ',')
    return ParsePrimary();

  // If this is a unary operator, read it.
  int Opc = CurTok;
  getNextToken();
  if (auto Operand = ParseUnary())
    return std::make_unique<UnaryExprAST>(Opc, std::move(Operand));
  return nullptr;
}

/// binoprhs
///   ::= ('+' unary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                              std::unique_ptr<ExprAST> LHS) {
  // If this is a binop, find its precedence.
  while (true) {
    int TokPrec = GetTokPrecedence();

    // If this is a binop that binds at least as tightly as the current binop,
    // consume it, otherwise we are done.
    if (TokPrec < ExprPrec)
      return LHS;

    // Okay, we know this is a binop.
    int BinOp = CurTok;
    getNextToken(); // eat binop

    // Parse the unary expression after the binary operator.
    auto RHS = ParseUnary();
    if (!RHS)
      return nullptr;

    // If BinOp binds less tightly with RHS than the operator after RHS, let
    // the pending operator take RHS as its LHS.
    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
      if (!RHS)
        return nullptr;
    }

    // Merge LHS/RHS.
    LHS =
        std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

/// expression
///   ::= unary binoprhs
///
static std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParseUnary();
  if (!LHS)
    return nullptr;

  return ParseBinOpRHS(0, std::move(LHS));
}

/// prototype
///   ::= id '(' id* ')'
///   ::= binary LETTER number? (id, id)
///   ::= unary LETTER (id)
static std::unique_ptr<PrototypeAST> ParsePrototype() {
  std::string FnName;

  unsigned Kind = 0; // 0 = identifier, 1 = unary, 2 = binary.
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

    // Read the precedence if present.
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

  // success.
  getNextToken(); // eat ')'.

  // Verify right number of names for operator.
  if (Kind && ArgNames.size() != Kind)
    return LogErrorP("Invalid number of operands for operator");

  return std::make_unique<PrototypeAST>(FnName, ArgNames, Kind != 0,
                                         BinaryPrecedence);
}

/// definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
  fprintf(stderr, "DEBUG: ParseDefinition() entered\n");
  
  getNextToken(); // eat def.

  fprintf(stderr, "DEBUG: ParseDefinition() - parsing prototype...\n");
  auto Proto = ParsePrototype();
  if (!Proto) {
    fprintf(stderr, "DEBUG: ParseDefinition() - ParsePrototype() FAILED!\n");
    return nullptr;
  }
  fprintf(stderr, "DEBUG: ParseDefinition() - prototype parsed: %s\n", Proto->getName().c_str());

  fprintf(stderr, "DEBUG: ParseDefinition() - parsing expression...\n");
  if (auto E = ParseExpression()) {
    fprintf(stderr, "DEBUG: ParseDefinition() - expression parsed successfully\n");
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  
  fprintf(stderr, "DEBUG: ParseDefinition() - ParseExpression() FAILED!\n");
  return nullptr;
}

/// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  fprintf(stderr, "DEBUG: ParseTopLevelExpr() entered\n");
  if (auto E = ParseExpression()) {
    // Make an anonymous proto.
    auto Proto = std::make_unique<PrototypeAST>("__anon_expr",
                                                 std::vector<std::string>());
    fprintf(stderr, "DEBUG: ParseTopLevelExpr() - expression parsed successfully\n");
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  fprintf(stderr, "DEBUG: ParseTopLevelExpr() - expression parsing failed\n");
  return nullptr;
}

/// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat extern.
  return ParsePrototype();
}

//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, AllocaInst *> NamedValues;
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
static ExitOnError ExitOnErr;

Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

Function *getFunction(std::string Name) {
  // First, see if the function has already been added to the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  // If no existing prototype exists, return null.
  return nullptr;
}

/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
/// the function.  This is used for mutable variables etc.
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          StringRef VarName) {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(Type::getDoubleTy(*TheContext), nullptr, VarName);
}

Value *NumberExprAST::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen() {
  // Look this variable up in the function.
  Value *V = NamedValues[Name];
  if (!V)
    return LogErrorV("Unknown variable name");

  // Load the value.
  return Builder->CreateLoad(Type::getDoubleTy(*TheContext), V, Name.c_str());
}

// --- StringExprAST codegen DEFINITION ---
Value *StringExprAST::codegen() {
  return Builder->CreateGlobalString(Val);  // Use CreateGlobalString, not CreateGlobalStringPtr
}


Value *UnaryExprAST::codegen() {
  Value *OperandV = Operand->codegen();
  if (!OperandV)
    return nullptr;

  Function *F = getFunction(std::string("unary") + Opcode);
  if (!F)
    return LogErrorV("Unknown unary operator");

  return Builder->CreateCall(F, OperandV, "unop");
}

Value *BinaryExprAST::codegen() {
  // Special case '=' because we don't want to emit the LHS as an expression.
  if (Op == '=') {
    // Assignment requires the LHS to be an identifier.
    // This assume we're building without RTTI because LLVM builds that way by
    // default.  If you build LLVM with RTTI this can be changed to a
    // dynamic_cast for automatic error checking.
    VariableExprAST *LHSE = static_cast<VariableExprAST *>(LHS.get());
    if (!LHSE)
      return LogErrorV("destination of '=' must be a variable");
    // Codegen the RHS.
    Value *Val = RHS->codegen();
    if (!Val)
      return nullptr;

    // Look up the name.
    Value *Variable = NamedValues[LHSE->getName()];
    if (!Variable)
      return LogErrorV("Unknown variable name");

    Builder->CreateStore(Val, Variable);
    return Val;
  }

  Value *L = LHS->codegen();
  Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;

  switch (Op) {
  case '+':
    return Builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return Builder->CreateFSub(L, R, "subtmp");
  case '*':
    return Builder->CreateFMul(L, R, "multmp");
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    break;
  }

  // If it wasn't a builtin binary operator, it must be a user defined one. Emit
  // a call to it.
  Function *F = getFunction(std::string("binary") + Op);
  assert(F && "binary operator not found!");

  Value *Ops[] = {L, R};
  return Builder->CreateCall(F, Ops, "binop");
}

Value *CallExprAST::codegen() {
  // Look up the name in the global module table.
  Function *CalleeF = getFunction(Callee);
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  // If argument mismatch error.
  if (CalleeF->arg_size() != Args.size())
    return LogErrorV("Incorrect # arguments passed");

  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->codegen());
    if (!ArgsV.back())
      return nullptr;
  }

  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Value *IfExprAST::codegen() {
  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;

  // Convert condition to a bool by comparing non-equal to 0.0.
  CondV = Builder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Create blocks for the then and else cases.  Insert the 'then' block at the
  // end of the function.
  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else");
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont");

  Builder->CreateCondBr(CondV, ThenBB, ElseBB);

  // Emit then value.
  Builder->SetInsertPoint(ThenBB);

  Value *ThenV = Then->codegen();
  if (!ThenV)
    return nullptr;

  Builder->CreateBr(MergeBB);
  // Codegen of 'Then' can change the current block, update ThenBB for the PHI.
  ThenBB = Builder->GetInsertBlock();

  // Emit else block.
  TheFunction->insert(TheFunction->end(), ElseBB);
  Builder->SetInsertPoint(ElseBB);

  Value *ElseV = Else->codegen();
  if (!ElseV)
    return nullptr;

  Builder->CreateBr(MergeBB);
  // Codegen of 'Else' can change the current block, update ElseBB for the PHI.
  ElseBB = Builder->GetInsertBlock();

  // Emit merge block.
  TheFunction->insert(TheFunction->end(), MergeBB);
  Builder->SetInsertPoint(MergeBB);
  PHINode *PN = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");

  PN->addIncoming(ThenV, ThenBB);
  PN->addIncoming(ElseV, ElseBB);
  return PN;
}

// Output for-loop as:
//   var = alloca double
//   ...
//   start = startexpr
//   store start -> var
//   goto loop
// loop:
//   ...
//   bodyexpr
//   ...
// loopend:
//   step = stepexpr
//   endcond = endexpr
//
//   curvar = load var
//   nextvar = curvar + step
//   store nextvar -> var
//   br endcond, loop, endloop
// outloop:
Value *ForExprAST::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Create an alloca for the variable in the entry block.
  AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);

  // Emit the start code first, without 'variable' in scope.
  Value *StartVal = Start->codegen();
  if (!StartVal)
    return nullptr;

  // Store the value into the alloca.
  Builder->CreateStore(StartVal, Alloca);

  // Make the new basic block for the loop header, inserting after current
  // block.
  BasicBlock *LoopBB = BasicBlock::Create(*TheContext, "loop", TheFunction);

  // Insert an explicit fall through from the current block to the LoopBB.
  Builder->CreateBr(LoopBB);

  // Start insertion in LoopBB.
  Builder->SetInsertPoint(LoopBB);

  // Within the loop, the variable is defined equal to the PHI node.  If it
  // shadows an existing variable, we have to restore it, so save it now.
  AllocaInst *OldVal = NamedValues[VarName];
  NamedValues[VarName] = Alloca;

  // Emit the body of the loop.  This, like any other expr, can change the
  // current BB.  Note that we ignore the value computed by the body, but don't
  // allow an error.
  if (!Body->codegen())
    return nullptr;

  // Emit the step value.
  Value *StepVal = nullptr;
  if (Step) {
    StepVal = Step->codegen();
    if (!StepVal)
      return nullptr;
  } else {
    // If not specified, use 1.0.
    StepVal = ConstantFP::get(*TheContext, APFloat(1.0));
  }

  // Compute the end condition.
  Value *EndCond = End->codegen();
  if (!EndCond)
    return nullptr;

  // Reload, increment, and restore the alloca.  This handles the case where
  // the body of the loop mutates the variable.
  Value *CurVar = Builder->CreateLoad(Type::getDoubleTy(*TheContext), Alloca,
                                      VarName.c_str());
  Value *NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
  Builder->CreateStore(NextVar, Alloca);

  // Convert condition to a bool by comparing non-equal to 0.0.
  EndCond = Builder->CreateFCmpONE(
      EndCond, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");

  // Create the "after loop" block and insert it.
  BasicBlock *AfterBB =
      BasicBlock::Create(*TheContext, "afterloop", TheFunction);

  // Insert the conditional branch into the end of LoopEndBB.
  Builder->CreateCondBr(EndCond, LoopBB, AfterBB);

  // Any new code will be inserted in AfterBB.
  Builder->SetInsertPoint(AfterBB);

  // Restore the unshadowed variable.
  if (OldVal)
    NamedValues[VarName] = OldVal;
  else
    NamedValues.erase(VarName);

  // for expr always returns 0.0.
  return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

Value *VarExprAST::codegen() {
  std::vector<AllocaInst *> OldBindings;

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Register all variables and emit their initializer.
  for (unsigned i = 0, e = VarNames.size(); i != e; ++i) {
    const std::string &VarName = VarNames[i].first;
    ExprAST *Init = VarNames[i].second.get();

    // Emit the initializer before adding the variable to scope, this prevents
    // the initializer from referencing the variable itself, and permits stuff
    // like this:
    //  var a = 1 in
    //    var a = a in ...   # refers to outer 'a'.
    Value *InitVal;
    if (Init) {
      InitVal = Init->codegen();
      if (!InitVal)
        return nullptr;
    } else { // If not specified, use 0.0.
      InitVal = ConstantFP::get(*TheContext, APFloat(0.0));
    }

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    Builder->CreateStore(InitVal, Alloca);

    // Remember the old variable binding so that we can restore the binding when
    // we unrecurse.
    OldBindings.push_back(NamedValues[VarName]);

    // Remember this binding.
    NamedValues[VarName] = Alloca;
  }

  // Codegen the body, now that all vars are in scope.
  Value *BodyVal = Body->codegen();
  if (!BodyVal)
    return nullptr;

  // Pop all our variables from scope.
  for (unsigned i = 0, e = VarNames.size(); i != e; ++i)
    NamedValues[VarNames[i].first] = OldBindings[i];

  // Return the body computation.
  return BodyVal;
}

Function *PrototypeAST::codegen() {
  // Make the function type:  double(double,double) etc.
  std::vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
  FunctionType *FT =
      FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  return F;
}

Function *FunctionAST::codegen() {
  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  auto &P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);
  Function *TheFunction = getFunction(P.getName());
  if (!TheFunction)
    return nullptr;

  // If this is an operator, install it.
  if (P.isBinaryOp())
    BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

  // Create a new basic block to start insertion into.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  // Record the function arguments in the NamedValues map.
  NamedValues.clear();
  for (auto &Arg : TheFunction->args()) {
    // Create an alloca for this variable.
    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());

    // Store the initial value into the alloca.
    Builder->CreateStore(&Arg, Alloca);

    // Add arguments to variable symbol table.
    NamedValues[std::string(Arg.getName())] = Alloca;
  }

  if (Value *RetVal = Body->codegen()) {
    // Finish off the function.
    Builder->CreateRet(RetVal);

    // Validate the generated code, checking for consistency.
    verifyFunction(*TheFunction);

    return TheFunction;
  }

  // Error reading body, remove function.
  TheFunction->eraseFromParent();

  if (P.isBinaryOp())
    BinopPrecedence.erase(P.getOperatorName());
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------------------------------------===//

static void InitializeModuleAndPassManager() {
  // Open a new module.
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("my cool jit", *TheContext);

  // Create a new builder for the module.
  Builder = std::make_unique<IRBuilder<>>(*TheContext);
}

static void HandleDefinition() {
  fprintf(stderr, "DEBUG: Entering HandleDefinition\n");
  fprintf(stderr, "DEBUG: About to call ParseDefinition()\n");
  
  if (auto FnAST = ParseDefinition()) {
    fprintf(stderr, "DEBUG: ParseDefinition() succeeded!\n");
    if (auto *FnIR = FnAST->codegen()) {
      fprintf(stderr, "Read function definition:");
      FnIR->print(errs());
      fprintf(stderr, "\n");
    }
  } else {
    fprintf(stderr, "DEBUG: ParseDefinition() returned nullptr!\n");
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern() {
  if (auto ProtoAST = ParseExtern()) {
    if (auto *FnIR = ProtoAST->codegen()) {
      fprintf(stderr, "Read extern: ");
      FnIR->print(errs());
      fprintf(stderr, "\n");
      FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (auto FnAST = ParseTopLevelExpr()) {
    FnAST->codegen();
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

/// top ::= definition | external | expression | ';'
static void MainLoop() {
  while (true) {
    switch (CurTok) {
    case tok_eof:
      return;
    case ';': // ignore top-level semicolons.
      getNextToken();
      break;
    case tok_def:
      fprintf(stderr, "DEBUG: tok_def found in MainLoop\n");
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}

//===----------------------------------------------------------------------===//
// "Library" functions that can be "extern'd" from user code.
//===----------------------------------------------------------------------===//

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/// putchard - putchar that takes a double and returns 0.
extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

/// printd - printf that takes a double prints it as "%f\n", returning 0.
extern "C" DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f\n", X);
  return 0;
}

/// printstr - prints a string literal
extern "C" DLLEXPORT double printstr(const char* Str) {
  fprintf(stderr, "%s", Str);
  return 0;
}

/// inputd - reads a double from stdin
extern "C" DLLEXPORT double inputd() {
  double X;
  scanf("%lf", &X);
  return X;
}

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//


static void EmitObjectFile(const std::string &Filename, bool LinkToExe = false, const std::string &ExeFile = "") {
    // --- DIAGNOSTIC: Print all functions in the module ---
    outs() << "Functions in module before emitting object file:\n";
    for (auto &F : *TheModule) {
        outs() << "  Function: " << F.getName() << "\n";
    }
    outs() << "---\n";

    // --- Step 1: Emit Object File ---
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
    InitializeAllAsmPrinters();

    auto TargetTriple = sys::getDefaultTargetTriple();
    TheModule->setTargetTriple(Triple(TargetTriple));

    std::string Error;
    auto Target = TargetRegistry::lookupTarget(TheModule->getTargetTriple(), Error);
    if (!Target) {
        errs() << "Error: " << Error << "\n";
        return;
    }

    auto CPU = "generic";
    auto Features = "";
    TargetOptions opt;
    auto TheTargetMachine = Target->createTargetMachine(
        Triple(TargetTriple), CPU, Features, opt, Reloc::PIC_);

    if (!TheTargetMachine) {
        errs() << "Error: Could not create target machine\n";
        return;
    }

    TheModule->setDataLayout(TheTargetMachine->createDataLayout());

    std::error_code EC;
    raw_fd_ostream dest(Filename, EC, sys::fs::OF_None);
    if (EC) {
        errs() << "Could not open file: " << EC.message() << "\n";
        return;
    }

    legacy::PassManager pass;
    auto FileType = CodeGenFileType::ObjectFile;

    if (TheTargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
        errs() << "Error: Target machine can't emit an object file\n";
        return;
    }

    pass.run(*TheModule);
    dest.flush();
    outs() << "Wrote " << Filename << "\n";

    // --- Step 2: If linking to exe, detect entry point and link ---
    if (LinkToExe && !ExeFile.empty()) {
        // --- Detect Entry Point ---
        std::vector<std::string> CommonEntryPoints = {
            "main", "cattoshallmeow", "__init__", "start", "run", "entry", "go"
        };

        std::string EntryPoint;
        for (const auto& name : CommonEntryPoints) {
            if (TheModule->getFunction(name)) {
                EntryPoint = name;
                break;
            }
        }

        if (EntryPoint.empty()) {
            std::vector<std::string> UserFunctions;
            for (auto &F : *TheModule) {
                if (!F.isDeclaration() && F.getName() != "__anon_expr") {
                    UserFunctions.push_back(F.getName().str());
                }
            }
            if (UserFunctions.size() == 1) {
                EntryPoint = UserFunctions[0];
            } else if (!UserFunctions.empty()) {
                EntryPoint = UserFunctions.back();
            } else {
                EntryPoint = "__anon_expr";
            }
        }

        outs() << "Detected entry point: " << EntryPoint << "\n";

        // --- Step 3: Generate Wrapper in Memory ---
        std::stringstream WrapperStream;
        WrapperStream << "extern \"C\" double " << EntryPoint << "(double x);\n";
        WrapperStream << "int main() {\n";
        WrapperStream << "    double result = " << EntryPoint << "(10);\n";
        WrapperStream << "    return 0;\n";
        WrapperStream << "}\n";

        // --- Step 4: Pipe to clang++ via stdin ---
        std::string Command = "clang++ -x c++ - -o " + ExeFile + " " + Filename;
        outs() << "Linking: " << Command << "\n";

        FILE* pipe = popen(Command.c_str(), "w");
        if (pipe) {
            fwrite(WrapperStream.str().c_str(), 1, WrapperStream.str().size(), pipe);
            int status = pclose(pipe);
            if (status != 0) {
                errs() << "Error: Linking failed with code " << status << "\n";
            } else {
                outs() << "Successfully linked to " << ExeFile << "\n";
            }
        } else {
            errs() << "Error: Could not open pipe to clang++\n";
        }
    }
}

void LinkToExecutable(const std::string &ObjectFile, const std::string &OutputFile, const std::string &EntryPoint) {
    std::stringstream WrapperStream;

    // --- Include necessary headers and define printd ---
    WrapperStream << "#include <cstdio>\n";
    WrapperStream << "#include <cstdlib>\n\n";

    // Define printd directly in the wrapper so the linker can find it
    WrapperStream << "extern \"C\" double printd(double X) {\n";
    WrapperStream << "    fprintf(stderr, \"%f\\n\", X);\n";
    WrapperStream << "    return 0;\n";
    WrapperStream << "}\n\n";

    // --- Declare the user's entry point ---
    WrapperStream << "extern \"C\" double " << EntryPoint << "(double x);\n\n";

    // --- The actual main() function ---
    WrapperStream << "int main() {\n";
    WrapperStream << "    double result = " << EntryPoint << "(10);\n";
    WrapperStream << "    return 0;\n";
    WrapperStream << "}\n";

    // --- Use the portable w64devkit g++ from the toolchain folder (Windows path with quotes) ---
    std::string CompilerPath = "\"toolchain\\w64devkit\\bin\\g++.exe\"";

    stdfs::path TempDir = stdfs::temp_directory_path() / "meowmeow";
    stdfs::create_directories(TempDir);
    stdfs::path WrapperPath = TempDir / "wrapper.cpp";

    std::ofstream WrapperFile(WrapperPath.string());
    WrapperFile << WrapperStream.str();
    WrapperFile.close();

    // --- Compile wrapper to object file ---
    std::string CompileCmd = CompilerPath + " -c " + WrapperPath.string() + " -o " + TempDir.string() + "\\wrapper.o";
    outs() << "Compiling: " << CompileCmd << "\n";
    int CompileResult = system(CompileCmd.c_str());
    if (CompileResult != 0) {
        errs() << "Error: Failed to compile wrapper\n";
        return;
    }

    // --- Link both object files ---
    std::string LinkCmd = CompilerPath + " " + TempDir.string() + "\\wrapper.o " + ObjectFile + " -o " + OutputFile;
    outs() << "Linking: " << LinkCmd << "\n";
    int LinkResult = system(LinkCmd.c_str());
    if (LinkResult != 0) {
        errs() << "Error: Linking failed with code " << LinkResult << "\n";
    } else {
        outs() << "Successfully linked to " << OutputFile << "\n";
    }

    // --- Clean up ---
    stdfs::remove(WrapperPath);
    stdfs::remove(TempDir / "wrapper.o");
}
int main(int argc, char **argv) {
    // Install standard binary operators
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;

    bool FileMode = false;
    std::string OutputFile = "output.o";
    bool LinkToExe = false;

    fprintf(stderr, "DEBUG: main() started\n");  // <-- ADDED

    // --- Parse command-line arguments ---
    for (int i = 1; i < argc; ++i) {
        std::string Arg = argv[i];
        fprintf(stderr, "DEBUG: Arg[%d] = %s\n", i, Arg.c_str());  // <-- ADDED

        if (Arg == "-o" && i + 1 < argc) {
            OutputFile = argv[++i];
            fprintf(stderr, "DEBUG: Output file = %s\n", OutputFile.c_str());  // <-- ADDED
            if (OutputFile.size() >= 4 &&
                OutputFile.substr(OutputFile.size() - 4) == ".exe") {
                LinkToExe = true;
                fprintf(stderr, "DEBUG: Linking to EXE\n");  // <-- ADDED
            }
        } else if (!FileMode && Arg[0] != '-') {
            fprintf(stderr, "DEBUG: Opening file: %s\n", Arg.c_str());  // <-- ADDED
            if (freopen(Arg.c_str(), "r", stdin) == nullptr) {
                fprintf(stderr, "Could not open file: %s\n", Arg.c_str());
                return 1;
            }
            fprintf(stderr, "DEBUG: File opened successfully\n");  // <-- ADDED
            FileMode = true;
        }
    }

    // --- Initialize module ---
    FunctionProtos["printd"] = std::make_unique<PrototypeAST>("printd", std::vector<std::string>{"x"});
    InitializeModuleAndPassManager();
    getNextToken();

    if (FileMode) {
        fprintf(stderr, "DEBUG: Entering file mode\n");  // <-- ADDED

        // --- Parse the entire file ---
        while (CurTok != tok_eof) {
            fprintf(stderr, "DEBUG: CurTok = %d\n", CurTok);  // <-- ADDED
            switch (CurTok) {
            case ';':
                fprintf(stderr, "DEBUG: Found ';'\n");  // <-- ADDED
                getNextToken();
                break;
            case tok_def:
                fprintf(stderr, "DEBUG: Found tok_def (%d)\n", tok_def);  // <-- ADDED
                HandleDefinition();
                break;
            case tok_extern:
                fprintf(stderr, "DEBUG: Found tok_extern\n");  // <-- ADDED
                HandleExtern();
                break;
            default:
                fprintf(stderr, "DEBUG: Unknown token, calling HandleTopLevelExpression\n");  // <-- ADDED
                HandleTopLevelExpression();
                break;
            }
        }

        fprintf(stderr, "DEBUG: Reached EOF, parsing complete\n");  // <-- ADDED

        // --- Determine object file name ---
        std::string ObjectFile = OutputFile;
        if (LinkToExe) {
            ObjectFile = OutputFile.substr(0, OutputFile.size() - 4) + ".o";
        }

        // --- Emit object file ---
        EmitObjectFile(ObjectFile);

        // --- If linking to exe, detect entry point and link ---
        if (LinkToExe) {
            // --- Detect Entry Point from the module ---
            std::string EntryPoint;
            std::vector<std::string> CommonEntryPoints = {
                "main", "cattoshallmeow", "__init__", "start", "run", "entry", "go"
            };

            for (const auto& name : CommonEntryPoints) {
                if (TheModule->getFunction(name)) {
                    EntryPoint = name;
                    break;
                }
            }

            if (EntryPoint.empty()) {
                for (auto &F : *TheModule) {
                    if (!F.isDeclaration() && F.getName() != "__anon_expr") {
                        EntryPoint = F.getName().str();
                        break;
                    }
                }
            }

            if (EntryPoint.empty()) {
                EntryPoint = "__anon_expr";
            }

            outs() << "Detected entry point: " << EntryPoint << "\n";

            // --- Link with the detected entry point ---
            LinkToExecutable(ObjectFile, OutputFile, EntryPoint);
        }
    } else {
        fprintf(stderr, "DEBUG: Entering REPL mode\n");  // <-- ADDED
        // --- REPL mode ---
        MainLoop();
    }

    fprintf(stderr, "DEBUG: main() finished\n");  // <-- ADDED
    return 0;
}