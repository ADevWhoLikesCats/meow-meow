#ifndef CODEGEN_H
#define CODEGEN_H

#include "AST.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

// Global LLVM state
extern std::unique_ptr<llvm::LLVMContext> TheContext;
extern std::unique_ptr<llvm::Module> TheModule;
extern std::unique_ptr<llvm::IRBuilder<>> Builder;
extern std::map<std::string, llvm::AllocaInst *> NamedValues;
extern std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
extern std::vector<std::string> LinkLibraries;

// Function lookup
llvm::Function *getFunction(std::string Name);

// Entry block allocator
llvm::AllocaInst *CreateEntryBlockAlloca(llvm::Function *TheFunction, llvm::StringRef VarName);

// Module initialization
void InitializeModuleAndPassManager();

// Object file emission and linking (with target support)
void EmitObjectFile(const std::string &Filename, bool LinkToExe = false, const std::string &ExeFile = "", const std::string &TargetTriple = "");

// Top-level handlers
void HandleDefinition();
void HandleExtern();
void HandleTopLevelExpression();

// REPL loop
void MainLoop();

#endif