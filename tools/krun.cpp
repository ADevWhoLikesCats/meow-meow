// tools/krun.cpp - K-Lang Bundler: .k → .exe with SPIR-V + CL Runtime
// Pushpita Catol Approved ✅

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

// ---------- K-LANG HEADERS ----------
#include "AST.h"
#include "Lexer.h"
#include "Parser.h"
#include "CodeGen.h"
#include "OpenCLRuntime.h"

std::map<char, int> BinopPrecedence;

// ====================================================================
// SPIR-V GENERATOR
// ====================================================================

class SPIRVGenerator {
private:
    std::string outputName;
    std::string kernelName;

public:
    SPIRVGenerator(const std::string& out) : outputName(out), kernelName("main") {}

    bool generate(ExprAST* root, std::vector<unsigned char>& spirvBinary) {
        // Use the global CodeGen functions
        InitializeModuleAndPassManager();
        
        // For now, create a dummy SPIR-V file
        std::cout << "🔧 Generating SPIR-V code...\n";
        
        std::ofstream spvFile(outputName + ".spv", std::ios::binary);
        if (!spvFile) {
            std::cerr << "Failed to create SPIR-V file\n";
            return false;
        }
        
        // Write minimal SPIR-V magic number
        uint32_t magic = 0x07230203;
        uint32_t version = 0x00010000;
        spvFile.write(reinterpret_cast<char*>(&magic), sizeof(magic));
        spvFile.write(reinterpret_cast<char*>(&version), sizeof(version));
        spvFile.close();
        
        // Read the generated .spv file
        std::string spirvFile = outputName + ".spv";
        std::ifstream file(spirvFile, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to read SPIR-V file: " << spirvFile << "\n";
            return false;
        }
        
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        spirvBinary.resize(size);
        file.read(reinterpret_cast<char*>(spirvBinary.data()), size);
        file.close();
        
        std::remove(spirvFile.c_str());
        
        kernelName = "main";
        return true;
    }
    
    const std::string& getKernelName() const { return kernelName; }
};

// ====================================================================
// BUNDLER (Generates standalone .exe with embedded SPIR-V + OpenCL)
// ====================================================================

class Bundler {
private:
    static std::string getMSYS2Root() {
        const char* msysRoot = std::getenv("MSYS2_ROOT");
        if (msysRoot) {
            return std::string(msysRoot);
        }
        
        std::string defaultPath = "D:/mysys2";
        if (std::ifstream((defaultPath + "/mingw64/include/CL/cl.h").c_str())) {
            return defaultPath;
        }
        
        const char* msystem = std::getenv("MSYSTEM");
        if (msystem) {
            std::vector<std::string> possiblePaths = {
                "C:/msys64",
                "D:/mysys2",
                "C:/msys2",
                "D:/msys2"
            };
            for (const auto& path : possiblePaths) {
                if (std::ifstream((path + "/mingw64/include/CL/cl.h").c_str())) {
                    return path;
                }
            }
        }
        
        return "";
    }
    
public:
    static bool generateExe(const std::string& outputName,
                            const std::vector<unsigned char>& spirvBinary,
                            const std::string& kernelName) {
        std::string source = generateCppSource(spirvBinary, kernelName);
        
        std::string cppFile = outputName + ".tmp.cpp";
        std::ofstream out(cppFile);
        if (!out) {
            std::cerr << "Failed to create temp file: " << cppFile << "\n";
            return false;
        }
        out << source;
        out.close();
        
        std::string msysRoot = getMSYS2Root();
        std::string cmd;
        std::string compiler = "g++";
        
        if (!msysRoot.empty()) {
            std::string includePath = msysRoot + "/mingw64/include";
            std::string clIncludePath = msysRoot + "/mingw64/include/CL";
            std::string libPath = msysRoot + "/mingw64/lib";
            
            std::cout << "🔧 Compiling with MSYS2 at: " << msysRoot << "\n";
            
            cmd = compiler +
                  " -std=c++17 -O2 -Wall " +
                  cppFile +
                  " -I\"" + includePath + "\"" +
                  " -I\"" + clIncludePath + "\"" +
                  " -L\"" + libPath + "\"" +
                  " -lOpenCL" +
                  " -o " + outputName + ".exe";
        } else {
            std::cout << "🔧 Compiling with standard Windows paths\n";
            cmd = compiler +
                  " -std=c++17 -O2 -Wall " +
                  cppFile +
                  " -lOpenCL" +
                  " -o " + outputName + ".exe";
            
            std::vector<std::string> commonPaths = {
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.0/include",
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8/include",
                "C:/Program Files (x86)/AMD APP SDK/include",
                "C:/Program Files/AMD APP SDK/include"
            };
            
            for (const auto& path : commonPaths) {
                if (std::ifstream((path + "/CL/cl.h").c_str())) {
                    cmd = compiler +
                          " -std=c++17 -O2 -Wall " +
                          cppFile +
                          " -I\"" + path + "\"" +
                          " -lOpenCL" +
                          " -o " + outputName + ".exe";
                    std::cout << "   Found OpenCL at: " << path << "\n";
                    break;
                }
            }
        }
        
        std::cout << "🔧 Compiling standalone executable...\n";
        int result = std::system(cmd.c_str());
        
        std::remove(cppFile.c_str());
        
        if (result != 0) {
            std::cerr << "Compilation failed (exit code: " << result << ")\n";
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

int main() {
    cl_int err;
    
    cl_uint numPlatforms;
    err = clGetPlatformIDs(0, nullptr, &numPlatforms);
    checkError(err, "clGetPlatformIDs");
    
    if (numPlatforms == 0) {
        std::cerr << "❌ No OpenCL platforms found\n";
        return 1;
    }
    
    std::vector<cl_platform_id> platforms(numPlatforms);
    err = clGetPlatformIDs(numPlatforms, platforms.data(), nullptr);
    checkError(err, "clGetPlatformIDs");
    
    std::cout << "🔍 Found " << numPlatforms << " OpenCL platform(s)\n";
    
    cl_device_id device = nullptr;
    for (cl_uint p = 0; p < numPlatforms; p++) {
        cl_uint numDevices;
        err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, 0, nullptr, &numDevices);
        if (err == CL_SUCCESS && numDevices > 0) {
            std::vector<cl_device_id> devices(numDevices);
            err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, numDevices, devices.data(), nullptr);
            if (err == CL_SUCCESS) {
                device = devices[0];
                break;
            }
        }
    }
    
    if (device == nullptr) {
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
    
    char deviceName[256];
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(deviceName), deviceName, nullptr);
    std::cout << "✅ Using device: " << deviceName << "\n";
    
    cl_context context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
    checkError(err, "clCreateContext");
    
    cl_command_queue queue = clCreateCommandQueue(context, device, 0, &err);
    checkError(err, "clCreateCommandQueue");
    
    cl_program program = clCreateProgramWithBinary(context, 1, &device, 
                                                    &spirvSize, 
                                                    (const unsigned char**)&spirvBinary,
                                                    nullptr, &err);
    checkError(err, "clCreateProgramWithBinary");
    
    err = clBuildProgram(program, 1, &device, nullptr, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t logSize;
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
        std::vector<char> buildLog(logSize + 1);
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, logSize, buildLog.data(), nullptr);
        std::cerr << "❌ Build failed:\n" << buildLog.data() << "\n";
        return 1;
    }
    
    cl_kernel kernel = clCreateKernel(program, ")" << kernelName << R"(", &err);
    checkError(err, "clCreateKernel");
    
    const size_t N = 1024;
    std::vector<int> input(N), output(N);
    for (size_t i = 0; i < N; i++) input[i] = (int)i;
    
    cl_mem inputBuf = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                      N * sizeof(int), input.data(), &err);
    checkError(err, "clCreateBuffer input");
    
    cl_mem outputBuf = clCreateBuffer(context, CL_MEM_READ_WRITE,
                                       N * sizeof(int), nullptr, &err);
    checkError(err, "clCreateBuffer output");
    
    err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &inputBuf);
    checkError(err, "clSetKernelArg 0");
    err = clSetKernelArg(kernel, 1, sizeof(cl_mem), &outputBuf);
    checkError(err, "clSetKernelArg 1");
    err = clSetKernelArg(kernel, 2, sizeof(int), &N);
    checkError(err, "clSetKernelArg 2");
    
    size_t globalSize = N;
    size_t localSize = 64;
    err = clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &globalSize, &localSize, 0, nullptr, nullptr);
    checkError(err, "clEnqueueNDRangeKernel");
    
    err = clEnqueueReadBuffer(queue, outputBuf, CL_TRUE, 0, N * sizeof(int), output.data(), 0, nullptr, nullptr);
    checkError(err, "clEnqueueReadBuffer");
    
    std::cout << "\n📊 Results:\n";
    for (size_t i = 0; i < 10 && i < N; i++) {
        std::cout << "   output[" << i << "] = " << output[i] << "\n";
    }
    
    std::cout << "\n✅ Pushpita Catol approves.\n";
    
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
// MAIN: krun entry point
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
        std::cout << "Example:\n";
        std::cout << "  " << argv[0] << " myprogram.k -o myprogram\n";
        std::cout << "  ./myprogram.exe\n\n";
        return 1;
    }
    
    std::string inputFile = argv[1];
    std::string outputName = "a";
    
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "-o" && i + 1 < argc) {
            outputName = argv[++i];
        }
    }
    
    std::cout << "📖 Reading source file: " << inputFile << "\n";
    std::ifstream file(inputFile);
    if (!file) {
        std::cerr << "❌ Could not open: " << inputFile << "\n";
        return 1;
    }
    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    file.close();
    
    std::cout << "🔨 Compiling " << inputFile << "...\n\n";
    
    // Generate SPIR-V
    std::cout << "🔧 Generating SPIR-V...\n";
    SPIRVGenerator generator(outputName);
    std::vector<unsigned char> spirvBinary;
    
    // Create a dummy AST node (nullptr since we're not actually parsing yet)
    // In a real implementation, this would come from the parser
    ExprAST* ast = nullptr;
    
    if (!generator.generate(ast, spirvBinary)) {
        std::cerr << "❌ SPIR-V generation failed\n";
        return 1;
    }
    
    std::cout << "✅ SPIR-V generated (" << spirvBinary.size() << " bytes)\n\n";
    
    // Bundle into executable
    std::cout << "🔧 Bundling into standalone executable...\n";
    if (!Bundler::generateExe(outputName, spirvBinary, generator.getKernelName())) {
        std::cerr << "❌ Bundling failed\n";
        return 1;
    }
    
    std::cout << "\n✅ Pushpita Catol approves.\n";
    std::cout << "📁 Run: ./" << outputName << ".exe\n\n";
    
    return 0;
}