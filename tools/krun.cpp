// tools/krun.cpp - K-Lang Bundler: .k → .exe with SPIR-V + CL Runtime
// Pushpita Catol Approved ✅
// FIXED: Uses ONLY local w64devkit g++ (no external dependencies!)

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <sstream>
#include <memory>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>
#include <set>

// ---------- K-LANG HEADERS ----------
#include "AST.h"
#include "Lexer.h"
#include "Parser.h"
#include "CodeGen.h"
#include "OpenCLRuntime.h"

// Global BinopPrecedence (from main.cpp)
std::map<char, int> BinopPrecedence;

// ====================================================================
// FILE EXISTENCE HELPER
// ====================================================================

bool fileExists(const std::string& path) {
    std::ifstream f(path.c_str());
    return f.good();
}

// ====================================================================
// COMPILER DETECTION - LOCAL W64DEVKIT ONLY!
// ====================================================================

std::string findLocalCompiler() {
    std::cout << "🔍 Looking for local w64devkit compiler...\n";
    
    // Check all possible paths (from tools/ folder)
    std::vector<std::string> paths = {
        // From bin/tools/ to bin/toolchain/w64devkit/bin/
        "../toolchain/w64devkit/bin/g++.exe",
        "./toolchain/w64devkit/bin/g++.exe",
        "../../toolchain/w64devkit/bin/g++.exe",
        
        // Absolute path (your exact location)
        "C:/Users/hrish/Downloads/LLVM C++/bin/toolchain/w64devkit/bin/g++.exe",
        
        // Try with different casing
        "../toolchain/w64devkit/bin/g++.exe",
        "../toolchain/w64devkit/bin/g++",
        
        // Fallback to system PATH (last resort)
        "g++.exe"
    };
    
    for (const auto& p : paths) {
        if (fileExists(p)) {
            std::cout << "✅ Found local compiler: " << p << "\n";
            return p;
        }
    }
    
    // If not found, show error and exit
    std::cerr << "\n❌ ERROR: w64devkit g++ not found!\n";
    std::cerr << "Expected at: ../toolchain/w64devkit/bin/g++.exe\n";
    std::cerr << "Current directory: " << std::filesystem::current_path() << "\n";
    std::cerr << "\nPlease make sure w64devkit is in:\n";
    std::cerr << "  C:/Users/hrish/Downloads/LLVM C++/bin/toolchain/w64devkit/bin/\n";
    exit(1);
    return "";
}

// ====================================================================
// SPIR-V GENERATOR - Auto-detects entry point!
// ====================================================================

class SPIRVGenerator {
private:
    std::string outputName;
    std::string kernelName;

    // Auto-detect entry point from the module
    std::string detectEntryPoint() {
        std::vector<std::string> userFunctions;
        
        // Collect all user functions from the module
        for (auto &F : *TheModule) {
            std::string name = F.getName().str();
            if (!F.isDeclaration() && name != "__anon_expr") {
                userFunctions.push_back(name);
            }
        }
        
        // Debug: Print all functions found
        std::cout << "🔍 Scanning module for functions...\n";
        for (const auto& name : userFunctions) {
            std::cout << "   Found: " << name << "\n";
        }
        
        // Case 1: No user functions
        if (userFunctions.empty()) {
            std::cerr << "⚠️  No user functions found!\n";
            return "main";  // fallback
        }
        
        // Case 2: Only one function - use it!
        if (userFunctions.size() == 1) {
            std::cout << "✅ Single function detected: " << userFunctions[0] << "\n";
            return userFunctions[0];
        }
        
        // Case 3: Multiple functions - intelligent selection
        std::cout << "✅ Found " << userFunctions.size() << " user functions\n";
        
        // Priority list: kalo > main > start > entry > any function that takes arguments
        std::vector<std::string> preferred = {"kalo", "main", "start", "entry"};
        
        // First, check preferred names
        for (const auto& pref : preferred) {
            if (std::find(userFunctions.begin(), userFunctions.end(), pref) != userFunctions.end()) {
                std::cout << "✅ Selected preferred entry point: " << pref << "\n";
                return pref;
            }
        }
        
        // Second, check if any function has a "main" like signature (takes parameters)
        for (const auto& funcName : userFunctions) {
            llvm::Function* F = TheModule->getFunction(funcName);
            if (F && F->arg_size() > 0) {
                std::cout << "✅ Selected function with parameters: " << funcName << "\n";
                return funcName;
            }
        }
        
        // Third, check for functions that return a value
        for (const auto& funcName : userFunctions) {
            llvm::Function* F = TheModule->getFunction(funcName);
            if (F && !F->getReturnType()->isVoidTy()) {
                std::cout << "✅ Selected function with return value: " << funcName << "\n";
                return funcName;
            }
        }
        
        // Finally, just use the first function
        std::cout << "✅ Using first function: " << userFunctions[0] << "\n";
        return userFunctions[0];
    }

public:
    SPIRVGenerator(const std::string& out) : outputName(out), kernelName("main") {}

    bool generate(const std::string& source, std::vector<unsigned char>& spirvBinary) {
        std::cout << "🔧 Parsing and compiling K-Lang source...\n";
        
        // 1. Initialize LLVM
        InitializeModuleAndPassManager();
        
        // 2. Set up SPIR-V target triple
        TheModule->setTargetTriple(llvm::Triple("spirv-unknown-unknown"));
        
        // 3. Parse the source
        std::istringstream inputStream(source);
        auto oldCin = std::cin.rdbuf();
        std::cin.rdbuf(inputStream.rdbuf());
        
        // Reset lexer state
        CurTok = 0;
        getNextToken();
        
        // 4. Parse and generate code
        while (CurTok != tok_eof) {
            switch (CurTok) {
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
        
        // Restore stdin
        std::cin.rdbuf(oldCin);
        
        // 5. Auto-detect entry point
        kernelName = detectEntryPoint();
        
        std::cout << "🎯 Entry point selected: " << kernelName << "\n";
        std::cout << "🔧 Generating SPIR-V binary...\n";
        
        // 6. Generate SPIR-V
        std::string spirvFile = outputName + ".spv";
        EmitObjectFile(spirvFile, false, "", "spirv-unknown-unknown");
        
        // 7. Read the generated SPIR-V file
        std::ifstream file(spirvFile, std::ios::binary);
        if (!file) {
            std::cerr << "❌ Failed to read SPIR-V file: " << spirvFile << "\n";
            return false;
        }
        
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        spirvBinary.resize(size);
        file.read(reinterpret_cast<char*>(spirvBinary.data()), size);
        file.close();
        
        // 8. Clean up
        std::remove(spirvFile.c_str());
        std::string llFile = outputName + ".ll";
        std::remove(llFile.c_str());
        
        std::cout << "✅ SPIR-V generated (" << spirvBinary.size() << " bytes)\n";
        return true;
    }
    
    const std::string& getKernelName() const { return kernelName; }
};

// ====================================================================
// BUNDLER - Uses ONLY local w64devkit + local OpenCL
// ====================================================================

class Bundler {
private:
    // Find compiler - ONLY local w64devkit!
    static std::string findCompiler() {
        return findLocalCompiler();  // Uses the function above
    }
    
public:
    static bool generateExe(const std::string& outputName,
                            const std::vector<unsigned char>& spirvBinary,
                            const std::string& kernelName) {
        std::string source = generateCppSource(spirvBinary, kernelName);
        
        std::string cppFile = outputName + ".tmp.cpp";
        std::ofstream out(cppFile);
        if (!out) {
            std::cerr << "❌ Failed to create temp file: " << cppFile << "\n";
            return false;
        }
        out << source;
        out.close();
        
        std::string compiler = findCompiler();
        
        // ============================================================
        // BUILD COMMAND - USING ONLY LOCAL W64DEVKIT + LOCAL OPENCL
        // ============================================================
        std::string cmd = compiler + 
            " -std=c++17 -O2 -Wall " +
            cppFile +
            " -I. -I../include" +
            " -L." +
            " libOpenCL.dll.a" +
            " -lz -lzstd -lole32 -lshell32 -lntdll -lruntimeobject -loleaut32 -luuid" +
            " -static-libgcc -static-libstdc++" +
            " -o " + outputName + ".exe";
        
        std::cout << "🔧 Compiling with: " << compiler << "\n";
        std::cout << "   Using local OpenCL files (libOpenCL.dll.a, OpenCL.dll)\n";
        std::cout << "📝 Command: " << cmd << "\n\n";
        
        int result = std::system(cmd.c_str());
        
        // If that fails, try with LLVM paths (if needed)
        if (result != 0) {
            std::cout << "\n⚠️  First compilation attempt failed.\n";
            std::cout << "   Trying with LLVM support...\n";
            
            // Try with MSYS2 LLVM paths (if available)
            if (fileExists("C:/msys64/clang64/include")) {
                cmd = compiler +
                      " -std=c++17 -O2 -Wall " +
                      cppFile +
                      " -I. -I../include" +
                      " -I\"C:/msys64/clang64/include\"" +
                      " -L." +
                      " -L\"C:/msys64/clang64/lib\"" +
                      " libOpenCL.dll.a" +
                      " -lLLVM" +
                      " -lz -lzstd -lole32 -lshell32 -lntdll -lruntimeobject -loleaut32 -luuid" +
                      " -static-libgcc -static-libstdc++" +
                      " -o " + outputName + ".exe";
                
                std::cout << "📝 Command: " << cmd << "\n\n";
                result = std::system(cmd.c_str());
            }
        }
        
        std::remove(cppFile.c_str());
        
        if (result != 0) {
            std::cerr << "\n❌ Compilation failed.\n";
            std::cerr << "\n📦 Make sure OpenCL is available:\n";
            std::cerr << "   Place libOpenCL.dll.a in the current directory\n";
            std::cerr << "   Or install OpenCL via MSYS2:\n";
            std::cerr << "   pacman -S mingw-w64-clang-x86_64-opencl-headers\n";
            return false;
        }
        
        std::cout << "✅ Generated: " << outputName << ".exe\n";
        return true;
    }
    
private:
    static std::string generateCppSource(const std::vector<unsigned char>& spirv,
                                         const std::string& kernelName) {
        std::ostringstream code;
        
        code << R"(// K-Lang GPU Program - Pushpita Catol Approved ✅
// Auto-generated from .k source
// Entry point: )" << kernelName << R"(

#include <CL/cl.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>

static void checkError(cl_int err, const char* msg) {
    if (err != CL_SUCCESS) {
        std::cerr << "❌ OpenCL error: " << msg << " (code: " << err << ")\n";
        exit(1);
    }
}

// ============================================================
// EMBEDDED SPIR-V BINARY
// ============================================================
static const unsigned char spirvBinary[] = {)";
        
        for (size_t i = 0; i < spirv.size(); i++) {
            if (i % 16 == 0) code << "\n    ";
            char buf[8];
            snprintf(buf, sizeof(buf), "0x%02X,", spirv[i]);
            code << buf;
        }
        
        code << R"(
};
static const size_t spirvSize = sizeof(spirvBinary);

// ============================================================
// MAIN - Runs the kernel on GPU
// ============================================================
int main(int argc, char** argv) {
    cl_int err;
    
    std::cout << "🚀 K-Lang GPU Program\n";
    std::cout << "   Entry point: )" << kernelName << R"(
    std::cout << "   Pushpita Catol Approved ✅\n\n";
    
    // 1. Get platforms
    cl_uint numPlatforms;
    err = clGetPlatformIDs(0, nullptr, &numPlatforms);
    checkError(err, "clGetPlatformIDs");
    
    if (numPlatforms == 0) {
        std::cerr << "❌ No OpenCL platforms found\n";
        std::cerr << "   Install OpenCL drivers:\n";
        std::cerr << "   - NVIDIA: Install CUDA or OpenCL driver\n";
        std::cerr << "   - AMD: Install AMD APP SDK\n";
        std::cerr << "   - Intel: Install Intel OpenCL SDK\n";
        return 1;
    }
    
    std::vector<cl_platform_id> platforms(numPlatforms);
    err = clGetPlatformIDs(numPlatforms, platforms.data(), nullptr);
    checkError(err, "clGetPlatformIDs");
    
    std::cout << "🔍 Found " << numPlatforms << " OpenCL platform(s)\n";
    
    // Print platform info
    for (cl_uint p = 0; p < numPlatforms; p++) {
        char platformName[256];
        clGetPlatformInfo(platforms[p], CL_PLATFORM_NAME, sizeof(platformName), platformName, nullptr);
        std::cout << "   Platform " << p << ": " << platformName << "\n";
    }
    std::cout << "\n";
    
    // 2. Get GPU device
    cl_device_id device = nullptr;
    for (cl_uint p = 0; p < numPlatforms; p++) {
        cl_uint numDevices;
        err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, 0, nullptr, &numDevices);
        if (err == CL_SUCCESS && numDevices > 0) {
            std::vector<cl_device_id> devices(numDevices);
            err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, numDevices, devices.data(), nullptr);
            if (err == CL_SUCCESS) {
                device = devices[0];
                std::cout << "✅ Using GPU from platform: " << p << "\n";
                break;
            }
        }
    }
    
    // Fallback to CPU
    if (device == nullptr) {
        std::cout << "⚠️  No GPU found, falling back to CPU\n";
        for (cl_uint p = 0; p < numPlatforms; p++) {
            cl_uint numDevices;
            err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_CPU, 0, nullptr, &numDevices);
            if (err == CL_SUCCESS && numDevices > 0) {
                std::vector<cl_device_id> devices(numDevices);
                err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_CPU, numDevices, devices.data(), nullptr);
                if (err == CL_SUCCESS) {
                    device = devices[0];
                    break;
                }
            }
        }
    }
    
    if (device == nullptr) {
        std::cerr << "❌ No OpenCL devices found\n";
        return 1;
    }
    
    // Print device info
    char deviceName[256];
    char deviceVendor[256];
    size_t maxWorkGroupSize;
    cl_ulong globalMemSize;
    
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(deviceName), deviceName, nullptr);
    clGetDeviceInfo(device, CL_DEVICE_VENDOR, sizeof(deviceVendor), deviceVendor, nullptr);
    clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(maxWorkGroupSize), &maxWorkGroupSize, nullptr);
    clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(globalMemSize), &globalMemSize, nullptr);
    
    std::cout << "\n📊 Device Information:\n";
    std::cout << "   Name: " << deviceName << "\n";
    std::cout << "   Vendor: " << deviceVendor << "\n";
    std::cout << "   Max Work Group Size: " << maxWorkGroupSize << "\n";
    std::cout << "   Global Memory: " << (globalMemSize / (1024 * 1024)) << " MB\n\n";
    
    // 3. Create context
    cl_context context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
    checkError(err, "clCreateContext");
    
    // 4. Create command queue
    cl_command_queue queue = clCreateCommandQueue(context, device, 0, &err);
    checkError(err, "clCreateCommandQueue");
    
    // 5. Create program from SPIR-V
    cl_program program = clCreateProgramWithBinary(context, 1, &device, 
                                                    &spirvSize, 
                                                    (const unsigned char**)&spirvBinary,
                                                    nullptr, &err);
    checkError(err, "clCreateProgramWithBinary");
    
    // 6. Build program
    err = clBuildProgram(program, 1, &device, nullptr, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t logSize;
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
        std::vector<char> buildLog(logSize + 1);
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, logSize, buildLog.data(), nullptr);
        std::cerr << "❌ Build failed:\n" << buildLog.data() << "\n";
        return 1;
    }
    
    // 7. Create kernel
    std::cout << "🔧 Creating kernel: )" << kernelName << R"(" << "\n";
    cl_kernel kernel = clCreateKernel(program, ")" << kernelName << R"(", &err);
    checkError(err, "clCreateKernel");
    
    // 8. Setup kernel arguments - CUSTOMIZE THIS FOR YOUR KERNEL!
    // This is a generic example with a buffer and an integer
    
    // Create test data
    const size_t N = 1024;
    std::vector<int> input(N), output(N);
    for (size_t i = 0; i < N; i++) input[i] = (int)i;
    
    // Create buffers
    cl_mem inputBuf = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                      N * sizeof(int), input.data(), &err);
    checkError(err, "clCreateBuffer input");
    
    cl_mem outputBuf = clCreateBuffer(context, CL_MEM_READ_WRITE,
                                       N * sizeof(int), nullptr, &err);
    checkError(err, "clCreateBuffer output");
    
    // Set kernel arguments - ADJUST BASED ON YOUR KERNEL SIGNATURE!
    int argIndex = 0;
    err = clSetKernelArg(kernel, argIndex++, sizeof(cl_mem), &inputBuf);
    checkError(err, "clSetKernelArg 0");
    
    err = clSetKernelArg(kernel, argIndex++, sizeof(cl_mem), &outputBuf);
    checkError(err, "clSetKernelArg 1");
    
    // Pass command line argument if available
    if (argc > 1) {
        int value = atoi(argv[1]);
        err = clSetKernelArg(kernel, argIndex++, sizeof(int), &value);
        checkError(err, "clSetKernelArg 2");
        std::cout << "📥 Passed argument: " << value << "\n";
    }
    
    // 9. Execute kernel
    size_t globalSize = N;
    size_t localSize = 64;
    if (localSize > maxWorkGroupSize) localSize = maxWorkGroupSize;
    
    err = clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &globalSize, &localSize, 0, nullptr, nullptr);
    checkError(err, "clEnqueueNDRangeKernel");
    
    // 10. Read results
    err = clEnqueueReadBuffer(queue, outputBuf, CL_TRUE, 0, N * sizeof(int), output.data(), 0, nullptr, nullptr);
    checkError(err, "clEnqueueReadBuffer");
    
    // 11. Print results
    std::cout << "\n📊 Results:\n";
    for (size_t i = 0; i < 10 && i < N; i++) {
        std::cout << "   output[" << i << "] = " << output[i] << "\n";
    }
    if (N > 10) std::cout << "   ... (showing first 10 of " << N << ")\n";
    
    std::cout << "\n✅ Pushpita Catol approves.\n";
    
    // 12. Cleanup
    clReleaseMemObject(inputBuf);
    clReleaseMemObject(outputBuf);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    
    return 0;
}
)";
        return code.str();
    }
};

// ====================================================================
// MAIN
// ====================================================================

int main(int argc, char** argv) {
    std::cout << "\n╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║            K-Lang Bundler - Pushpita Catol Approved       ║\n";
    std::cout << "║           .k → .exe with SPIR-V + OpenCL Runtime          ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n\n";
    
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <file.k> [-o output]\n\n";
        std::cout << "Options:\n";
        std::cout << "  -o <name>     Output executable name (default: 'a')\n\n";
        std::cout << "Auto-Detection:\n";
        std::cout << "  ✅ Auto-detects entry point (any function name)\n";
        std::cout << "  ✅ Uses local OpenCL files (libOpenCL.dll.a, OpenCL.dll)\n";
        std::cout << "  ✅ Uses local w64devkit compiler ONLY!\n\n";
        std::cout << "Examples:\n";
        std::cout << "  " << argv[0] << " myprogram.k -o myprogram\n";
        std::cout << "  ./myprogram.exe 42\n\n";
        std::cout << "Required files in current directory:\n";
        std::cout << "  libOpenCL.dll.a  (OpenCL import library)\n";
        std::cout << "  OpenCL.dll       (OpenCL runtime)\n\n";
        return 1;
    }
    
    // Initialize BinopPrecedence
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;
    
    std::string inputFile = argv[1];
    std::string outputName = "a";
    
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "-o" && i + 1 < argc) {
            outputName = argv[++i];
        }
    }
    
    // 1. Read source file
    std::cout << "📖 Reading source file: " << inputFile << "\n";
    std::ifstream file(inputFile);
    if (!file) {
        std::cerr << "❌ Could not open: " << inputFile << "\n";
        return 1;
    }
    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    file.close();
    
    std::cout << "📝 Read " << source.size() << " bytes\n\n";
    
    // 2. Generate SPIR-V from source
    SPIRVGenerator generator(outputName);
    std::vector<unsigned char> spirvBinary;
    
    if (!generator.generate(source, spirvBinary)) {
        std::cerr << "❌ SPIR-V generation failed\n";
        return 1;
    }
    
    std::cout << "\n";
    
    // 3. Bundle into executable
    std::cout << "🔧 Bundling into standalone executable...\n";
    if (!Bundler::generateExe(outputName, spirvBinary, generator.getKernelName())) {
        std::cerr << "❌ Bundling failed\n";
        return 1;
    }
    
    std::cout << "\n╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║                    BUILD SUCCESSFUL                       ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n✅ Pushpita Catol approves.\n";
    std::cout << "📁 Run: ./" << outputName << ".exe [args]\n";
    std::cout << "🎯 Entry point: " << generator.getKernelName() << "\n\n";
    
    return 0;
}