#include "AST.h"
#include "CodeGen.h"
#include "Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"

// ==================== CODEGEN IMPLEMENTATIONS ====================

llvm::Value *NumberExprAST::codegen() {
    return llvm::ConstantFP::get(*TheContext, llvm::APFloat(Val));
}

llvm::Value *StringExprAST::codegen() {
    return Builder->CreateGlobalString(Val);
}

llvm::Value *VariableExprAST::codegen() {
    llvm::Value *V = NamedValues[Name];
    if (!V) return LogErrorV("Unknown variable name");
    return Builder->CreateLoad(llvm::Type::getDoubleTy(*TheContext), V, Name.c_str());
}

llvm::Value *UnaryExprAST::codegen() {
    llvm::Value *OperandV = Operand->codegen();
    if (!OperandV) return nullptr;

    llvm::Function *F = getFunction(std::string("unary") + Opcode);
    if (!F) return LogErrorV("Unknown unary operator");

    return Builder->CreateCall(F, OperandV, "unop");
}




char PrototypeAST::getOperatorName() const {
    assert(isUnaryOp() || isBinaryOp());
    return Name[Name.size() - 1];
}

llvm::Value *BinaryExprAST::codegen() {
    if (Op == '=') {
        VariableExprAST *LHSE = static_cast<VariableExprAST *>(LHS.get());
        if (!LHSE) return LogErrorV("destination of '=' must be a variable");

        llvm::Value *Val = RHS->codegen();
        if (!Val) return nullptr;

        llvm::Value *Variable = NamedValues[LHSE->getName()];
        if (!Variable) return LogErrorV("Unknown variable name");

        Builder->CreateStore(Val, Variable);
        return Val;
    }

    llvm::Value *L = LHS->codegen();
    llvm::Value *R = RHS->codegen();
    if (!L || !R) return nullptr;

    switch (Op) {
    case '+':
        return Builder->CreateFAdd(L, R, "addtmp");
    case '-':
        return Builder->CreateFSub(L, R, "subtmp");
    case '*':
        return Builder->CreateFMul(L, R, "multmp");
    case '<':
        L = Builder->CreateFCmpULT(L, R, "cmptmp");
        return Builder->CreateUIToFP(L, llvm::Type::getDoubleTy(*TheContext), "booltmp");
    default:
        break;
    }

    llvm::Function *F = getFunction(std::string("binary") + Op);
    assert(F && "binary operator not found!");

    llvm::Value *Ops[] = {L, R};
    return Builder->CreateCall(F, Ops, "binop");
}

llvm::Value *CallExprAST::codegen() {
    llvm::Function *CalleeF = getFunction(Callee);
    if (!CalleeF) return LogErrorV("Unknown function referenced");

    if (CalleeF->arg_size() != Args.size())
        return LogErrorV("Incorrect # arguments passed");

    std::vector<llvm::Value *> ArgsV;
    for (unsigned i = 0, e = Args.size(); i != e; ++i) {
        ArgsV.push_back(Args[i]->codegen());
        if (!ArgsV.back()) return nullptr;
    }

    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

llvm::Value *IfExprAST::codegen() {
    llvm::Value *CondV = Cond->codegen();
    if (!CondV) return nullptr;

    CondV = Builder->CreateFCmpONE(
        CondV, llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0)), "ifcond");

    llvm::Function *TheFunction = Builder->GetInsertBlock()->getParent();

    llvm::BasicBlock *ThenBB = llvm::BasicBlock::Create(*TheContext, "then", TheFunction);
    llvm::BasicBlock *ElseBB = llvm::BasicBlock::Create(*TheContext, "else");
    llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(*TheContext, "ifcont");

    Builder->CreateCondBr(CondV, ThenBB, ElseBB);

    Builder->SetInsertPoint(ThenBB);
    llvm::Value *ThenV = Then->codegen();
    if (!ThenV) return nullptr;
    Builder->CreateBr(MergeBB);
    ThenBB = Builder->GetInsertBlock();

    TheFunction->insert(TheFunction->end(), ElseBB);
    Builder->SetInsertPoint(ElseBB);
    llvm::Value *ElseV = Else->codegen();
    if (!ElseV) return nullptr;
    Builder->CreateBr(MergeBB);
    ElseBB = Builder->GetInsertBlock();

    TheFunction->insert(TheFunction->end(), MergeBB);
    Builder->SetInsertPoint(MergeBB);

    llvm::PHINode *PN = Builder->CreatePHI(llvm::Type::getDoubleTy(*TheContext), 2, "iftmp");
    PN->addIncoming(ThenV, ThenBB);
    PN->addIncoming(ElseV, ElseBB);
    return PN;
}

llvm::Value *ForExprAST::codegen() {
    llvm::Function *TheFunction = Builder->GetInsertBlock()->getParent();

    llvm::AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);

    llvm::Value *StartVal = Start->codegen();
    if (!StartVal) return nullptr;
    Builder->CreateStore(StartVal, Alloca);

    llvm::BasicBlock *LoopBB = llvm::BasicBlock::Create(*TheContext, "loop", TheFunction);
    Builder->CreateBr(LoopBB);

    Builder->SetInsertPoint(LoopBB);

    llvm::AllocaInst *OldVal = NamedValues[VarName];
    NamedValues[VarName] = Alloca;

    if (!Body->codegen()) return nullptr;

    llvm::Value *StepVal = nullptr;
    if (Step) {
        StepVal = Step->codegen();
        if (!StepVal) return nullptr;
    } else {
        StepVal = llvm::ConstantFP::get(*TheContext, llvm::APFloat(1.0));
    }

    llvm::Value *EndCond = End->codegen();
    if (!EndCond) return nullptr;

    llvm::Value *CurVar = Builder->CreateLoad(llvm::Type::getDoubleTy(*TheContext), Alloca, VarName.c_str());
    llvm::Value *NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
    Builder->CreateStore(NextVar, Alloca);

    EndCond = Builder->CreateFCmpONE(
        EndCond, llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0)), "loopcond");

    llvm::BasicBlock *AfterBB = llvm::BasicBlock::Create(*TheContext, "afterloop", TheFunction);
    Builder->CreateCondBr(EndCond, LoopBB, AfterBB);

    Builder->SetInsertPoint(AfterBB);

    if (OldVal)
        NamedValues[VarName] = OldVal;
    else
        NamedValues.erase(VarName);

    return llvm::Constant::getNullValue(llvm::Type::getDoubleTy(*TheContext));
}

llvm::Value *VarExprAST::codegen() {
    std::vector<llvm::AllocaInst *> OldBindings;

    llvm::Function *TheFunction = Builder->GetInsertBlock()->getParent();

    for (unsigned i = 0, e = VarNames.size(); i != e; ++i) {
        const std::string &VarName = VarNames[i].first;
        ExprAST *Init = VarNames[i].second.get();

        llvm::Value *InitVal;
        if (Init) {
            InitVal = Init->codegen();
            if (!InitVal) return nullptr;
        } else {
            InitVal = llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0));
        }

        llvm::AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
        Builder->CreateStore(InitVal, Alloca);

        OldBindings.push_back(NamedValues[VarName]);
        NamedValues[VarName] = Alloca;
    }

    llvm::Value *BodyVal = Body->codegen();
    if (!BodyVal) return nullptr;

    for (unsigned i = 0, e = VarNames.size(); i != e; ++i)
        NamedValues[VarNames[i].first] = OldBindings[i];

    return BodyVal;
}

llvm::Value *LetExprAST::codegen() {
    // Get the current function
    llvm::Function *TheFunction = Builder->GetInsertBlock()->getParent();

    // Codegen the initializer
    llvm::Value *InitVal = Init->codegen();
    if (!InitVal) return nullptr;

    // Create an alloca for the variable
    llvm::AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Name);

    // Store the initial value
    Builder->CreateStore(InitVal, Alloca);

    // Add the variable to the symbol table
    NamedValues[Name] = Alloca;

    // Return the initial value
    return InitVal;
}


llvm::Value *AsmBlockExprAST::codegen() {
    // Join all instructions into a single string with newlines and tabs
    std::string AsmString;
    for (size_t i = 0; i < Instructions.size(); ++i) {
        if (i > 0) {
            AsmString += "\n\t";
        }
        AsmString += Instructions[i];
    }

    // Function type: void()
    llvm::FunctionType *FTy = llvm::FunctionType::get(llvm::Type::getVoidTy(*TheContext), false);

    // Constraint string: empty for no operands
    std::string Constraints = "";
    
    // Has side effects? Yes.
    bool HasSideEffects = true;

    // Choose the dialect
    llvm::InlineAsm::AsmDialect Dialect = UseIntelSyntax 
        ? llvm::InlineAsm::AD_Intel 
        : llvm::InlineAsm::AD_ATT;

    // Create the inline assembly object
    llvm::InlineAsm *IA = llvm::InlineAsm::get(
        FTy,
        AsmString,
        Constraints,
        HasSideEffects,
        false,   // IsAlignStack
        Dialect
    );

    // Call it
    return Builder->CreateCall(IA);
}


llvm::Value *AsmExprAST::codegen() {
    llvm::FunctionType *FTy = llvm::FunctionType::get(llvm::Type::getVoidTy(*TheContext), false);
    std::string Constraints = "";
    bool HasSideEffects = true;

    llvm::InlineAsm *IA = llvm::InlineAsm::get(FTy, AsmString, Constraints, HasSideEffects);
    return Builder->CreateCall(IA);
}

llvm::Function *PrototypeAST::codegen() {
    std::vector<llvm::Type *> Doubles(Args.size(), llvm::Type::getDoubleTy(*TheContext));
    llvm::FunctionType *FT = llvm::FunctionType::get(llvm::Type::getDoubleTy(*TheContext), Doubles, false);

    llvm::Function *F = llvm::Function::Create(FT, llvm::Function::ExternalLinkage, Name, TheModule.get());

    unsigned Idx = 0;
    for (auto &Arg : F->args())
        Arg.setName(Args[Idx++]);

    return F;
}

llvm::Value *AddressOfExprAST::codegen() {
    llvm::Value *V = Operand->codegen();
    if (!V) return nullptr;
    // Get the address of the variable
    return V;
}

llvm::Value *DerefExprAST::codegen() {
    llvm::Value *V = Operand->codegen();
    if (!V) return nullptr;
    // Load from the pointer
    return Builder->CreateLoad(llvm::Type::getInt32Ty(*TheContext), V);
}

llvm::Value *NullExprAST::codegen() {
    return llvm::Constant::getNullValue(llvm::Type::getInt32Ty(*TheContext));
}

llvm::Value *PointerTypeAST::codegen() {
    // This is a type, not a value — return null
    return nullptr;
}


llvm::Function *FunctionAST::codegen() {
    auto &P = *Proto;
    FunctionProtos[Proto->getName()] = std::move(Proto);

    llvm::Function *TheFunction = getFunction(P.getName());
    if (!TheFunction) return nullptr;

    if (P.isBinaryOp())
        BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

    llvm::BasicBlock *BB = llvm::BasicBlock::Create(*TheContext, "entry", TheFunction);
    Builder->SetInsertPoint(BB);

    NamedValues.clear();
    for (auto &Arg : TheFunction->args()) {
        llvm::AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());
        Builder->CreateStore(&Arg, Alloca);
        NamedValues[std::string(Arg.getName())] = Alloca;
    }

    if (llvm::Value *RetVal = Body->codegen()) {
        Builder->CreateRet(RetVal);
        llvm::verifyFunction(*TheFunction);
        return TheFunction;
    }

    TheFunction->eraseFromParent();

    if (P.isBinaryOp())
        BinopPrecedence.erase(P.getOperatorName());
    return nullptr;
}