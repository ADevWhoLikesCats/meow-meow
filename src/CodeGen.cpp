#include "CodeGen.h"
#include "Parser.h"
#include "Library.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace stdfs = std::filesystem;

// ==================== LLVM GLOBALS ====================

std::unique_ptr<llvm::LLVMContext> TheContext;
std::unique_ptr<llvm::Module> TheModule;
std::unique_ptr<llvm::IRBuilder<>> Builder;
std::map<std::string, llvm::AllocaInst *> NamedValues;
std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

// ==================== FUNCTION LOOKUP ====================

llvm::Function *getFunction(std::string Name) {
    if (auto *F = TheModule->getFunction(Name))
        return F;

    auto FI = FunctionProtos.find(Name);
    if (FI != FunctionProtos.end())
        return FI->second->codegen();

    return nullptr;
}

// ==================== ENTRY BLOCK ALLOCATOR ====================

llvm::AllocaInst *CreateEntryBlockAlloca(llvm::Function *TheFunction, llvm::StringRef VarName) {
    llvm::IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                           TheFunction->getEntryBlock().begin());
    return TmpB.CreateAlloca(llvm::Type::getDoubleTy(*TheContext), nullptr, VarName);
}

// ==================== MODULE INITIALIZATION ====================

void InitializeModuleAndPassManager() {
    TheContext = std::make_unique<llvm::LLVMContext>();
    TheModule = std::make_unique<llvm::Module>("my cool jit", *TheContext);
    Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);
}

// ==================== TOP-LEVEL HANDLERS ====================

void HandleDefinition() {
    if (auto FnAST = ParseDefinition()) {
        if (auto *FnIR = FnAST->codegen()) {
            fprintf(stderr, "Read function definition:");
            FnIR->print(llvm::errs());
            fprintf(stderr, "\n");
        }
    } else {
        getNextToken();
    }
}

void HandleExtern() {
    if (auto ProtoAST = ParseExtern()) {
        if (auto *FnIR = ProtoAST->codegen()) {
            fprintf(stderr, "Read extern: ");
            FnIR->print(llvm::errs());
            fprintf(stderr, "\n");
            FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
        }
    } else {
        getNextToken();
    }
}

void HandleTopLevelExpression() {
    if (auto FnAST = ParseTopLevelExpr()) {
        FnAST->codegen();
    } else {
        getNextToken();
    }
}

void MainLoop() {
    while (true) {
        switch (CurTok) {
        case tok_eof:
            return;
        case ';':
            getNextToken();
            break;
        case tok_def:
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

// ==================== EMIT OBJECT FILE ====================

void EmitObjectFile(const std::string &Filename, bool LinkToExe, const std::string &ExeFile) {
    // --- DIAGNOSTIC: Print all functions in the module ---
    llvm::outs() << "Functions in module before emitting object file:\n";
    for (auto &F : *TheModule) {
        llvm::outs() << "  Function: " << F.getName() << "\n";
    }
    llvm::outs() << "---\n";

    // --- Step 1: Emit Object File ---
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    auto TargetTriple = llvm::sys::getDefaultTargetTriple();
    TheModule->setTargetTriple(llvm::Triple(TargetTriple));

    std::string Error;
    auto Target = llvm::TargetRegistry::lookupTarget(TheModule->getTargetTriple(), Error);
    if (!Target) {
        llvm::errs() << "Error: " << Error << "\n";
        return;
    }

    auto CPU = "generic";
    auto Features = "";
    llvm::TargetOptions opt;
    auto TheTargetMachine = Target->createTargetMachine(
        llvm::Triple(TargetTriple), CPU, Features, opt, llvm::Reloc::PIC_);

    if (!TheTargetMachine) {
        llvm::errs() << "Error: Could not create target machine\n";
        return;
    }

    TheModule->setDataLayout(TheTargetMachine->createDataLayout());

    std::error_code EC;
    llvm::raw_fd_ostream dest(Filename, EC, llvm::sys::fs::OF_None);
    if (EC) {
        llvm::errs() << "Could not open file: " << EC.message() << "\n";
        return;
    }

    llvm::legacy::PassManager pass;
    auto FileType = llvm::CodeGenFileType::ObjectFile;

    if (TheTargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
        llvm::errs() << "Error: Target machine can't emit an object file\n";
        return;
    }

    pass.run(*TheModule);
    dest.flush();
    llvm::outs() << "Wrote " << Filename << "\n";

    // --- Step 2: If linking to exe, detect entry point and link ---
    if (LinkToExe && !ExeFile.empty()) {
        // --- Auto-detect entry point without hardcoding ---
        std::string EntryPoint;

        std::vector<std::string> UserFunctions;
        for (auto &F : *TheModule) {
            std::string Name = F.getName().str();
            if (!F.isDeclaration() && Name != "__anon_expr") {
                UserFunctions.push_back(Name);
            }
        }

        if (UserFunctions.size() == 1) {
            EntryPoint = UserFunctions[0];
            llvm::outs() << "Auto-detected entry point: " << EntryPoint << "\n";
        } else if (UserFunctions.size() > 1) {
            if (TheModule->getFunction("main")) {
                EntryPoint = "main";
                llvm::outs() << "Using 'main' as entry point (convention)\n";
            } else {
                EntryPoint = UserFunctions[0];
                llvm::outs() << "Warning: Multiple functions found. Using first: " << EntryPoint << "\n";
            }
        } else {
            EntryPoint = "__anon_expr";
            llvm::outs() << "No functions found. Using __anon_expr (this will fail)\n";
        }

        llvm::outs() << "Detected entry point: " << EntryPoint << "\n";

        // --- Step 3: Generate Wrapper in Memory ---
        std::stringstream WrapperStream;
        WrapperStream << "#include <cstdio>\n";
        WrapperStream << "#include <cstdlib>\n\n";

        // Define printd
        WrapperStream << "extern \"C\" double printd(double X) {\n";
        WrapperStream << "    fprintf(stderr, \"%f\\n\", X);\n";
        WrapperStream << "    return 0;\n";
        WrapperStream << "}\n\n";

        // Define printstr
        WrapperStream << "extern \"C\" double printstr(const char* Str) {\n";
        WrapperStream << "    fprintf(stderr, \"%s\", Str);\n";
        WrapperStream << "    return 0;\n";
        WrapperStream << "}\n\n";

        // Define inputd
        WrapperStream << "extern \"C\" double inputd() {\n";
        WrapperStream << "    double X;\n";
        WrapperStream << "    char buffer[128];\n";
        WrapperStream << "    int hasNumber = 0;\n";
        WrapperStream << "    do {\n";
        WrapperStream << "        fprintf(stderr, \"Enter a number: \");\n";
        WrapperStream << "        fflush(stderr);\n";
        WrapperStream << "        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {\n";
        WrapperStream << "            clearerr(stdin);\n";
        WrapperStream << "            continue;\n";
        WrapperStream << "        }\n";
        WrapperStream << "        if (sscanf(buffer, \"%lf\", &X) == 1) hasNumber = 1;\n";
        WrapperStream << "        if (!hasNumber) {\n";
        WrapperStream << "            fprintf(stderr, \"Invalid input. Please enter a number.\\n\");\n";
        WrapperStream << "        }\n";
        WrapperStream << "    } while (!hasNumber);\n";
        WrapperStream << "    return X;\n";
        WrapperStream << "}\n\n";

        // Declare the user's entry point
        WrapperStream << "extern \"C\" double " << EntryPoint << "(double x);\n\n";

        // The actual main() function with command-line argument support
        WrapperStream << "int main(int argc, char** argv) {\n";
        WrapperStream << "    double x = (argc > 1) ? atof(argv[1]) : 10;\n";
        WrapperStream << "    double result = " << EntryPoint << "(x);\n";
        WrapperStream << "    return 0;\n";
        WrapperStream << "}\n";

        // --- Step 4: Write wrapper to file and compile ---
        stdfs::path TempDir = stdfs::temp_directory_path() / "meowmeow";
        stdfs::create_directories(TempDir);
        stdfs::path WrapperPath = TempDir / "wrapper.cpp";

        std::ofstream WrapperFile(WrapperPath.string());
        WrapperFile << WrapperStream.str();
        WrapperFile.close();

        // --- Step 5: Compile wrapper to object file ---
        std::string CompilerPath = "\"toolchain\\w64devkit\\bin\\g++.exe\"";
        std::string CompileCmd = CompilerPath + " -c " + WrapperPath.string() + " -o " + TempDir.string() + "\\wrapper.o";
        llvm::outs() << "Compiling: " << CompileCmd << "\n";
        int CompileResult = system(CompileCmd.c_str());
        if (CompileResult != 0) {
            llvm::errs() << "Error: Failed to compile wrapper\n";
            return;
        }

        // --- Step 6: Link both object files ---
        std::string LinkCmd = CompilerPath + " " + TempDir.string() + "\\wrapper.o " + Filename + " -o " + ExeFile;
        llvm::outs() << "Linking: " << LinkCmd << "\n";
        int LinkResult = system(LinkCmd.c_str());
        if (LinkResult != 0) {
            llvm::errs() << "Error: Linking failed with code " << LinkResult << "\n";
        } else {
            llvm::outs() << "Successfully linked to " << ExeFile << "\n";
        }

        // --- Step 7: Clean up ---
        stdfs::remove(WrapperPath);
        stdfs::remove(TempDir / "wrapper.o");
    }
}

void LinkToExecutable(const std::string &ObjectFile, const std::string &OutputFile, const std::string &EntryPoint) {
    // This function is now integrated into EmitObjectFile.
    // It's kept here for compatibility but is no longer used directly.
    // The linking is handled inside EmitObjectFile when LinkToExe is true.
    llvm::outs() << "Note: LinkToExecutable is deprecated. Linking is now handled in EmitObjectFile.\n";
}