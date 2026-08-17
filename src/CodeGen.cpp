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
std::vector<std::string> LinkLibraries;

// ==================== .klib STRUCT ====================

struct KlibSymbol {
    std::string Name;
    uint32_t Offset;
    uint32_t Size;
};

// ==================== .klib READER ====================

std::vector<uint8_t> ReadKlib(const std::string &Filename, std::vector<KlibSymbol> &Symbols) {
    std::ifstream File(Filename, std::ios::binary);
    if (!File) {
        llvm::errs() << "Error: Could not open .klib file: " << Filename << "\n";
        return {};
    }

    char Magic[4];
    File.read(Magic, 4);
    if (strncmp(Magic, "KLIB", 4) != 0) {
        llvm::errs() << "Error: Invalid .klib file (wrong magic): " << Filename << "\n";
        return {};
    }

    uint32_t Version;
    File.read(reinterpret_cast<char*>(&Version), 4);
    if (Version != 1) {
        llvm::errs() << "Error: Unsupported .klib version: " << Version << "\n";
        return {};
    }

    uint32_t Count;
    File.read(reinterpret_cast<char*>(&Count), 4);

    Symbols.resize(Count);
    for (uint32_t i = 0; i < Count; ++i) {
        uint32_t NameLen;
        File.read(reinterpret_cast<char*>(&NameLen), 4);
        std::string Name(NameLen, ' ');
        File.read(&Name[0], NameLen);
        Symbols[i].Name = Name;

        File.read(reinterpret_cast<char*>(&Symbols[i].Offset), 4);
        File.read(reinterpret_cast<char*>(&Symbols[i].Size), 4);
    }

    File.seekg(0, std::ios::end);
    size_t CodeSize = File.tellg() - 16 - Count * 16;
    File.seekg(16 + Count * 16, std::ios::beg);

    std::vector<uint8_t> Code(CodeSize);
    File.read(reinterpret_cast<char*>(Code.data()), CodeSize);

    return Code;
}

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

void EmitObjectFile(const std::string &Filename, bool LinkToExe, const std::string &ExeFile, const std::string &TargetTriple) {
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

    // Use the provided target triple, or fallback to default
    std::string Triple = TargetTriple.empty() ? llvm::sys::getDefaultTargetTriple() : TargetTriple;
    TheModule->setTargetTriple(llvm::Triple(Triple));

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
        llvm::Triple(Triple), CPU, Features, opt, llvm::Reloc::PIC_);

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
    llvm::outs() << "Wrote " << Filename << " for target: " << Triple << "\n";

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

        // --- Step 6: Link object file + .klib files ---
        std::string LinkCmd = CompilerPath + " " + TempDir.string() + "\\wrapper.o " + Filename;

        // Add .klib libraries
        for (const auto &Lib : LinkLibraries) {
            std::string KlibPath = Lib + ".klib";
            if (stdfs::exists(KlibPath)) {
                LinkCmd += " " + KlibPath;
                llvm::outs() << "Linking library: " << KlibPath << "\n";
            } else {
                llvm::errs() << "Warning: Library not found: " << KlibPath << "\n";
            }
        }

        LinkCmd += " -o " + ExeFile;
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