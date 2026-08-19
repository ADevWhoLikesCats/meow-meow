// include/OpenCLRuntime.h - OpenCL Runtime for K-Lang
// Pushpita Catol Approved ✅

#pragma once

#include <CL/opencl.hpp>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <fstream>
#include <iostream>

namespace KLang {

enum class AddressSpace {
    PRIVATE,
    GLOBAL,
    LOCAL,
    CONSTANT
};

struct KernelArgument {
    std::string name;
    AddressSpace addressSpace;
    size_t size;
    bool isPointer;
    std::string typeName;
};

struct KernelInfo {
    std::string name;
    std::vector<KernelArgument> arguments;
    size_t globalWorkSize;
    size_t localWorkSize;
    int workDim; // 1, 2, or 3
};

class OpenCLRuntime {
private:
    cl::Context context;
    cl::Device device;
    cl::CommandQueue queue;
    cl::Program program;
    
    std::map<std::string, cl::Kernel> kernels;
    std::map<std::string, std::vector<cl::Buffer>> buffers;
    std::map<std::string, KernelInfo> kernelInfos;
    
    bool initialized = false;
    std::string lastError;

public:
    OpenCLRuntime();
    ~OpenCLRuntime();
    
    // Initialize OpenCL
    bool initialize(bool preferGPU = true);
    
    // Load SPIR-V binary
    bool loadSPIRV(const std::vector<unsigned char>& spirvBinary, 
                   const std::vector<KernelInfo>& kernelInfos);
    
    bool loadSPIRVFromFile(const std::string& spirvFile,
                           const std::vector<KernelInfo>& kernelInfos);
    
    // Create buffers
    template<typename T>
    cl::Buffer createBuffer(const std::string& kernelName, 
                            int argIndex,
                            size_t count,
                            T* data = nullptr,
                            bool readOnly = false);
    
    // Set kernel arguments
    template<typename T>
    bool setKernelArg(const std::string& kernelName, int index, const T& value);
    
    bool setKernelArgBuffer(const std::string& kernelName, 
                            int index, 
                            const cl::Buffer& buffer);
    
    // Run kernel
    bool runKernel(const std::string& kernelName, 
                   size_t globalSize, 
                   size_t localSize = 64);
    
    bool runKernel2D(const std::string& kernelName,
                     size_t globalX, size_t globalY,
                     size_t localX = 8, size_t localY = 8);
    
    bool runKernel3D(const std::string& kernelName,
                     size_t globalX, size_t globalY, size_t globalZ,
                     size_t localX = 4, size_t localY = 4, size_t localZ = 4);
    
    // Read buffers
    template<typename T>
    bool readBuffer(const std::string& kernelName, 
                    int bufferIndex,
                    size_t count,
                    T* data);
    
    // Device info
    std::string getDeviceName() const;
    std::string getDeviceVendor() const;
    size_t getMaxWorkGroupSize() const;
    size_t getMaxComputeUnits() const;
    size_t getGlobalMemorySize() const;
    size_t getLocalMemorySize() const;
    std::string getDeviceVersion() const;
    std::string getDriverVersion() const;
    
    // Utility
    bool isInitialized() const { return initialized; }
    const std::string& getLastError() const { return lastError; }
    
    // Get kernel info
    const KernelInfo& getKernelInfo(const std::string& name) const;
    
private:
    bool selectDevice(bool preferGPU);
    bool buildProgram(const std::vector<unsigned char>& spirvBinary);
    void extractKernelInfo(const std::vector<KernelInfo>& infos);
};

// ====================================================================
// TEMPLATE IMPLEMENTATIONS
// ====================================================================

template<typename T>
cl::Buffer OpenCLRuntime::createBuffer(const std::string& kernelName,
                                       int argIndex,
                                       size_t count,
                                       T* data,
                                       bool readOnly) {
    if (!initialized) {
        lastError = "Runtime not initialized";
        return cl::Buffer();
    }
    
    cl_mem_flags flags = readOnly ? CL_MEM_READ_ONLY : CL_MEM_READ_WRITE;
    if (data) {
        flags |= CL_MEM_COPY_HOST_PTR;
    }
    
    cl::Buffer buffer(context, flags, count * sizeof(T), data);
    buffers[kernelName].push_back(buffer);
    
    // Set kernel argument
    auto it = kernels.find(kernelName);
    if (it != kernels.end()) {
        it->second.setArg(argIndex, buffer);
    }
    
    return buffer;
}

template<typename T>
bool OpenCLRuntime::setKernelArg(const std::string& kernelName, 
                                  int index, 
                                  const T& value) {
    auto it = kernels.find(kernelName);
    if (it == kernels.end()) {
        lastError = "Kernel not found: " + kernelName;
        return false;
    }
    
    it->second.setArg(index, value);
    return true;
}

template<typename T>
bool OpenCLRuntime::readBuffer(const std::string& kernelName,
                               int bufferIndex,
                               size_t count,
                               T* data) {
    auto it = buffers.find(kernelName);
    if (it == buffers.end()) {
        lastError = "No buffers for kernel: " + kernelName;
        return false;
    }
    
    if (bufferIndex >= (int)it->second.size()) {
        lastError = "Buffer index out of range";
        return false;
    }
    
    cl_int err = queue.enqueueReadBuffer(it->second[bufferIndex], 
                                         CL_TRUE, 
                                         0, 
                                         count * sizeof(T), 
                                         data);
    
    if (err != CL_SUCCESS) {
        lastError = "Failed to read buffer: " + std::to_string(err);
        return false;
    }
    
    return true;
}

} // namespace KLang