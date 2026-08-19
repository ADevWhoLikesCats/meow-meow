#include "Lexer.h"
#include "Parser.h"
#include "CodeGen.h"
#include "Library.h"
#include "Klib.h"          // <-- New header for .klib packaging
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"
#include <map>
#include <string>
#include <cstdio>
#include <vector>
#include <cstdlib>

// References
std::map<char, int> BinopPrecedence;

int main(int argc, char **argv) {
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;

    bool FileMode = false;
    std::string OutputFile = "output.o";
    bool LinkToExe = false;
    bool EmitKlib = false;
    std::string TargetTriple = llvm::sys::getDefaultTargetTriple();

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string Arg = argv[i];

        if (Arg == "-target" && i + 1 < argc) {
            TargetTriple = argv[++i];
            fprintf(stderr, "DEBUG: Target set to: %s\n", TargetTriple.c_str());
        }
        else if (Arg == "-o" && i + 1 < argc) {
            OutputFile = argv[++i];
            if (OutputFile.size() >= 4 && OutputFile.substr(OutputFile.size() - 4) == ".exe") {
                LinkToExe = true;
            }
        }
        else if (Arg == "-l" && i + 1 < argc) {
            LinkLibraries.push_back(argv[++i]);
            fprintf(stderr, "DEBUG: Linking library: %s.klib\n", argv[i]);
        }
        else if (Arg == "-emit-klib") {
            EmitKlib = true;
        }
        else if (!FileMode && Arg[0] != '-') {
            if (freopen(Arg.c_str(), "r", stdin) == nullptr) {
                fprintf(stderr, "Could not open file: %s\n", Arg.c_str());
                return 1;
            }
            FileMode = true;
        }
    }

    // If -emit-klib is set but no output file specified, use default
    if (EmitKlib && OutputFile == "output.o") {
        OutputFile = "a.klib";
    }

    // Initialize module
    FunctionProtos["printd"] = std::make_unique<PrototypeAST>("printd", std::vector<std::string>{"x"});
    FunctionProtos["printstr"] = std::make_unique<PrototypeAST>("printstr", std::vector<std::string>{"x"});
    FunctionProtos["inputd"] = std::make_unique<PrototypeAST>("inputd", std::vector<std::string>{});
    InitializeModuleAndPassManager();

    TheModule->setTargetTriple(llvm::Triple(TargetTriple));

    getNextToken();

    if (FileMode) {
        while (CurTok != tok_eof) {
            switch (CurTok) {
            case ';': getNextToken(); break;
            case tok_def:
                fprintf(stderr, "DEBUG: tok_def found in main loop\n");
                HandleDefinition();
                break;
            case tok_extern: HandleExtern(); break;
            default: HandleTopLevelExpression(); break;
            }
        }

        // Determine object file name
        std::string ObjectFile;
        if (EmitKlib || LinkToExe) {
            // If output is .klib, use a temporary .o
            if (OutputFile.size() >= 4 && OutputFile.substr(OutputFile.size() - 4) == ".klib") {
                ObjectFile = OutputFile.substr(0, OutputFile.size() - 4) + ".o";
            } else {
                ObjectFile = OutputFile + ".o";
            }
        } else {
            ObjectFile = OutputFile;
        }

        // Emit object file
        EmitObjectFile(ObjectFile, false, "", TargetTriple);

        // If -emit-klib, package as .klib
        if (EmitKlib) {
            EmitKlibFile(ObjectFile, OutputFile);
            // Clean up temporary .o
            std::remove(ObjectFile.c_str());
            llvm::outs() << "Packaged " << OutputFile << "\n";
        }

        // If linking to executable, do that
        if (LinkToExe) {
            // Re-emit object file with linking info (this is a simplified flow)
            // In a real implementation, you'd reuse the object file
            std::string ExeFile = OutputFile;
            EmitObjectFile(ObjectFile, true, ExeFile, TargetTriple);
        }

    } else {
        MainLoop();
    }

    return 0;
}