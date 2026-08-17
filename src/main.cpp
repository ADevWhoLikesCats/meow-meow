#include "Lexer.h"
#include "Parser.h"
#include "CodeGen.h"
#include "Library.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"
#include <map>
#include <string>
#include <cstdio>

// Define globals
std::map<char, int> BinopPrecedence;

int main(int argc, char **argv) {
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;

    bool FileMode = false;
    std::string OutputFile = "output.o";
    bool LinkToExe = false;
    std::string TargetTriple = llvm::sys::getDefaultTargetTriple(); // Default target

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string Arg = argv[i];

        // -target flag
        if (Arg == "-target" && i + 1 < argc) {
            TargetTriple = argv[++i];
            fprintf(stderr, "DEBUG: Target set to: %s\n", TargetTriple.c_str());
        }
        // -o flag
        else if (Arg == "-o" && i + 1 < argc) {
            OutputFile = argv[++i];
            if (OutputFile.size() >= 4 && OutputFile.substr(OutputFile.size() - 4) == ".exe") {
                LinkToExe = true;
            }
        }
        // Input file
        else if (!FileMode && Arg[0] != '-') {
            if (freopen(Arg.c_str(), "r", stdin) == nullptr) {
                fprintf(stderr, "Could not open file: %s\n", Arg.c_str());
                return 1;
            }
            FileMode = true;
        }
    }

    // Initialize module
    FunctionProtos["printd"] = std::make_unique<PrototypeAST>("printd", std::vector<std::string>{"x"});
    FunctionProtos["printstr"] = std::make_unique<PrototypeAST>("printstr", std::vector<std::string>{"x"});
    FunctionProtos["inputd"] = std::make_unique<PrototypeAST>("inputd", std::vector<std::string>{});
    InitializeModuleAndPassManager();

    // Set the target triple
    TheModule->setTargetTriple(llvm::Triple(TargetTriple));

    getNextToken();

    if (FileMode) {
        // Parse the entire file
        while (CurTok != tok_eof) {
            switch (CurTok) {
            case ';': getNextToken(); break;
            case tok_def: HandleDefinition(); break;
            case tok_extern: HandleExtern(); break;
            default: HandleTopLevelExpression(); break;
            }
        }

        // Determine object file name
        std::string ObjectFile = OutputFile;
        if (LinkToExe) {
            ObjectFile = OutputFile.substr(0, OutputFile.size() - 4) + ".o";
        }

        // Emit object file (and link if needed) with target
        EmitObjectFile(ObjectFile, LinkToExe, OutputFile, TargetTriple);
    } else {
        // REPL mode
        MainLoop();
    }

    return 0;
}