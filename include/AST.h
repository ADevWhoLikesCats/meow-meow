#ifndef AST_H
#define AST_H

#include "llvm/IR/Value.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include <memory>
#include <string>
#include <vector>

// Forward declarations
class ExprAST;
class PrototypeAST;
class FunctionAST;

// Base class
class ExprAST {
public:
    virtual ~ExprAST() = default;
    virtual llvm::Value *codegen() = 0;
};

// Number literal
class NumberExprAST : public ExprAST {
    double Val;
public:
    NumberExprAST(double Val) : Val(Val) {}
    llvm::Value *codegen() override;
};

// String literal
class StringExprAST : public ExprAST {
    std::string Val;
public:
    StringExprAST(const std::string &Val) : Val(Val) {}
    llvm::Value *codegen() override;
};

// Variable reference
class VariableExprAST : public ExprAST {
    std::string Name;
public:
    VariableExprAST(const std::string &Name) : Name(Name) {}
    llvm::Value *codegen() override;
    const std::string &getName() const { return Name; }
};

// Unary operator
class UnaryExprAST : public ExprAST {
    char Opcode;
    std::unique_ptr<ExprAST> Operand;
public:
    UnaryExprAST(char Opcode, std::unique_ptr<ExprAST> Operand)
        : Opcode(Opcode), Operand(std::move(Operand)) {}
    llvm::Value *codegen() override;
};

// Binary operator
class BinaryExprAST : public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;
public:
    BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS)
        : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
    llvm::Value *codegen() override;
};

// Function call
class CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;
public:
    CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args)
        : Callee(Callee), Args(std::move(Args)) {}
    llvm::Value *codegen() override;
};

// If expression
class IfExprAST : public ExprAST {
    std::unique_ptr<ExprAST> Cond, Then, Else;
public:
    IfExprAST(std::unique_ptr<ExprAST> Cond, std::unique_ptr<ExprAST> Then, std::unique_ptr<ExprAST> Else)
        : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}
    llvm::Value *codegen() override;
};

// For loop
class ForExprAST : public ExprAST {
    std::string VarName;
    std::unique_ptr<ExprAST> Start, End, Step, Body;
public:
    ForExprAST(const std::string &VarName, std::unique_ptr<ExprAST> Start,
               std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step,
               std::unique_ptr<ExprAST> Body)
        : VarName(VarName), Start(std::move(Start)), End(std::move(End)),
          Step(std::move(Step)), Body(std::move(Body)) {}
    llvm::Value *codegen() override;
};

// Variable definition (var/in)
class VarExprAST : public ExprAST {
    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;
    std::unique_ptr<ExprAST> Body;
public:
    VarExprAST(std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames,
               std::unique_ptr<ExprAST> Body)
        : VarNames(std::move(VarNames)), Body(std::move(Body)) {}
    llvm::Value *codegen() override;
};

/// Inline assembly block (ASM_INTEL/ASM_ATT ... END_ASM)
class AsmBlockExprAST : public ExprAST {
    std::vector<std::string> Instructions;
    bool UseIntelSyntax;

public:
    AsmBlockExprAST(const std::vector<std::string> &Instructions, bool UseIntelSyntax)
        : Instructions(Instructions), UseIntelSyntax(UseIntelSyntax) {}

    llvm::Value *codegen() override;
};

// Address-of expression (&x)
class AddressOfExprAST : public ExprAST {
    std::unique_ptr<ExprAST> Operand;
public:
    AddressOfExprAST(std::unique_ptr<ExprAST> Operand)
        : Operand(std::move(Operand)) {}
    llvm::Value *codegen() override;
};

// Dereference expression (*ptr)
class DerefExprAST : public ExprAST {
    std::unique_ptr<ExprAST> Operand;
public:
    DerefExprAST(std::unique_ptr<ExprAST> Operand)
        : Operand(std::move(Operand)) {}
    llvm::Value *codegen() override;
};

// Null pointer
class NullExprAST : public ExprAST {
public:
    NullExprAST() {}
    llvm::Value *codegen() override;
};



// Pointer type (for type declarations)
class PointerTypeAST : public ExprAST {
    std::unique_ptr<ExprAST> PointeeType;
public:
    PointerTypeAST(std::unique_ptr<ExprAST> PointeeType)
        : PointeeType(std::move(PointeeType)) {}
    llvm::Value *codegen() override;
};


// Inline assembly
class AsmExprAST : public ExprAST {
    std::string AsmString;
public:
    AsmExprAST(const std::string &AsmString) : AsmString(AsmString) {}
    llvm::Value *codegen() override;
};

/// LetExprAST - Represents a variable declaration: let name = expr
class LetExprAST : public ExprAST {
    std::string Name;
    std::unique_ptr<ExprAST> Init;

public:
    LetExprAST(const std::string &Name, std::unique_ptr<ExprAST> Init)
        : Name(Name), Init(std::move(Init)) {}

    llvm::Value *codegen() override;
};


// Prototype
class PrototypeAST {
    std::string Name;
    std::vector<std::string> Args;
    bool IsOperator;
    unsigned Precedence;
public:
    PrototypeAST(const std::string &Name, std::vector<std::string> Args,
                 bool IsOperator = false, unsigned Prec = 0)
        : Name(Name), Args(std::move(Args)), IsOperator(IsOperator), Precedence(Prec) {}
    llvm::Function *codegen();
    const std::string &getName() const { return Name; }
    bool isUnaryOp() const { return IsOperator && Args.size() == 1; }
    bool isBinaryOp() const { return IsOperator && Args.size() == 2; }
    char getOperatorName() const;
    unsigned getBinaryPrecedence() const { return Precedence; }
};

// Function definition
class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;
public:
    FunctionAST(std::unique_ptr<PrototypeAST> Proto, std::unique_ptr<ExprAST> Body)
        : Proto(std::move(Proto)), Body(std::move(Body)) {}
    llvm::Function *codegen();
};

#endif