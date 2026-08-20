// src/OpenCLRuntime.cpp - OpenCL Runtime Implementation
// Pushpita Catol Approved ✅

#include "OpenCLRuntime.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <CL/cl.h>

namespace KLang {

OpenCLRuntime::OpenCLRuntime() {
    initialized = false;
    context = nullptr;
    device = nullptr;
    queue = nullptr;
    program = nullptr;
}

OpenCLRuntime::~OpenCLRuntime() {
    if (queue) {
        clReleaseCommandQueue(queue);
        queue = nullptr;
    }
    if (program) {
        clReleaseProgram(program);
        program = nullptr;
    }
    if (context) {
        clReleaseContext(context);
        context = nullptr;
    }
    
    // Release kernels
    for (auto& kv : kernels) {
        if (kv.second) {
            clReleaseKernel(kv.second);
        }
    }
    kernels.clear();
    
    // Release buffers
    for (auto& kv : buffers) {
        for (auto& buf : kv.second) {
            if (buf) {
                clReleaseMemObject(buf);
            }
        }
    }
    buffers.clear();
}

bool OpenCLRuntime::initialize(bool preferGPU) {
    cl_int err;
    
    // Get platforms
    cl_uint numPlatforms;
    err = clGetPlatformIDs(0, nullptr, &numPlatforms);
    if (err != CL_SUCCESS) {
        lastError = "Failed to get platform count: " + std::to_string(err);
        return false;
    }
    
    if (numPlatforms == 0) {
        lastError = "No OpenCL platforms found";
        return false;
    }
    
    std::vector<cl_platform_id> platforms(numPlatforms);
    err = clGetPlatformIDs(numPlatforms, platforms.data(), nullptr);
    if (err != CL_SUCCESS) {
        lastError = "Failed to get platforms: " + std::to_string(err);
        return false;
    }
    
    // Select device
    if (!selectDevice(preferGPU, platforms)) {
        return false;
    }
    
    // Create context
    cl_context_properties props[] = {
        CL_CONTEXT_PLATFORM, (cl_context_properties)platforms[0],
        0
    };
    
    context = clCreateContext(props, 1, &device, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) {
        lastError = "Failed to create context: " + std::to_string(err);
        return false;
    }
    
    // Create command queue (use clCreateCommandQueueWithProperties for newer OpenCL)
    #ifdef CL_VERSION_2_0
    cl_queue_properties queueProps[] = {0};
    queue = clCreateCommandQueueWithProperties(context, device, queueProps, &err);
    #else
    queue = clCreateCommandQueue(context, device, 0, &err);
    #endif
    
    if (err != CL_SUCCESS) {
        lastError = "Failed to create command queue: " + std::to_string(err);
        clReleaseContext(context);
        context = nullptr;
        return false;
    }
    
    initialized = true;
    return true;
}

bool OpenCLRuntime::selectDevice(bool preferGPU, const std::vector<cl_platform_id>& platforms) {
    cl_int err;
    
    for (auto platform : platforms) {
        cl_uint numDevices;
        
        // Try GPU first
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &numDevices);
        if (err == CL_SUCCESS && numDevices > 0) {
            std::vector<cl_device_id> devices(numDevices);
            err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices.data(), nullptr);
            if (err == CL_SUCCESS) {
                device = devices[0];
                return true;
            }
        }
        
        // Try CPU
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 0, nullptr, &numDevices);
        if (err == CL_SUCCESS && numDevices > 0) {
            std::vector<cl_device_id> devices(numDevices);
            err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, numDevices, devices.data(), nullptr);
            if (err == CL_SUCCESS) {
                device = devices[0];
                return true;
            }
        }
    }
    
    lastError = "No suitable OpenCL device found";
    return false;
}

bool OpenCLRuntime::loadSPIRV(const std::vector<unsigned char>& spirvBinary,
                              const std::vector<KernelInfo>& kernelInfos) {
    if (!initialized) {
        lastError = "Runtime not initialized";
        return false;
    }
    
    cl_int err;
    
    // Create program from binary
    const unsigned char* binaryPtr = spirvBinary.data();
    size_t binarySize = spirvBinary.size();
    
    cl_program programCL = clCreateProgramWithBinary(
        context,
        1,
        &device,
        &binarySize,
        &binaryPtr,
        nullptr,
        &err
    );
    
    if (err != CL_SUCCESS) {
        lastError = "Failed to create program from SPIR-V: " + std::to_string(err);
        return false;
    }
    
    // Build program
    err = clBuildProgram(programCL, 1, &device, nullptr, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t logSize;
        clGetProgramBuildInfo(programCL, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
        std::vector<char> buildLog(logSize + 1);
        clGetProgramBuildInfo(programCL, device, CL_PROGRAM_BUILD_LOG, logSize, buildLog.data(), nullptr);
        lastError = "Build failed:\n" + std::string(buildLog.data());
        clReleaseProgram(programCL);
        return false;
    }
    
    // Store program
    program = programCL;
    
    // Create kernels
    for (const auto& info : kernelInfos) {
        cl_kernel kernelCL = clCreateKernel(programCL, info.name.c_str(), &err);
        if (err != CL_SUCCESS) {
            lastError = "Failed to create kernel: " + info.name;
            clReleaseProgram(programCL);
            return false;
        }
        kernels[info.name] = kernelCL;
        kernelInfosMap[info.name] = info;
    }
    
    return true;
}

bool OpenCLRuntime::loadSPIRVFromFile(const std::string& spirvFile,
                                      const std::vector<KernelInfo>& kernelInfos) {
    std::ifstream file(spirvFile, std::ios::binary);
    if (!file) {
        lastError = "Could not open SPIR-V file: " + spirvFile;
        return false;
    }
    
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<unsigned char> binary(size);
    file.read(reinterpret_cast<char*>(binary.data()), size);
    file.close();
    
    return loadSPIRV(binary, kernelInfos);
}

bool OpenCLRuntime::runKernel(const std::string& kernelName,
                              size_t globalSize,
                              size_t localSize) {
    auto it = kernels.find(kernelName);
    if (it == kernels.end()) {
        lastError = "Kernel not found: " + kernelName;
        return false;
    }
    
    cl_int err = clEnqueueNDRangeKernel(
        queue,
        it->second,
        1,
        nullptr,
        &globalSize,
        &localSize,
        0,
        nullptr,
        nullptr
    );
    
    if (err != CL_SUCCESS) {
        lastError = "Failed to enqueue kernel: " + std::to_string(err);
        return false;
    }
    
    clFinish(queue);
    return true;
}

bool OpenCLRuntime::runKernel2D(const std::string& kernelName,
                                size_t globalX, size_t globalY,
                                size_t localX, size_t localY) {
    auto it = kernels.find(kernelName);
    if (it == kernels.end()) {
        lastError = "Kernel not found: " + kernelName;
        return false;
    }
    
    size_t global[2] = {globalX, globalY};
    size_t local[2] = {localX, localY};
    
    cl_int err = clEnqueueNDRangeKernel(
        queue,
        it->second,
        2,
        nullptr,
        global,
        local,
        0,
        nullptr,
        nullptr
    );
    
    if (err != CL_SUCCESS) {
        lastError = "Failed to enqueue 2D kernel: " + std::to_string(err);
        return false;
    }
    
    clFinish(queue);
    return true;
}

bool OpenCLRuntime::runKernel3D(const std::string& kernelName,
                                size_t globalX, size_t globalY, size_t globalZ,
                                size_t localX, size_t localY, size_t localZ) {
    auto it = kernels.find(kernelName);
    if (it == kernels.end()) {
        lastError = "Kernel not found: " + kernelName;
        return false;
    }
    
    size_t global[3] = {globalX, globalY, globalZ};
    size_t local[3] = {localX, localY, localZ};
    
    cl_int err = clEnqueueNDRangeKernel(
        queue,
        it->second,
        3,
        nullptr,
        global,
        local,
        0,
        nullptr,
        nullptr
    );
    
    if (err != CL_SUCCESS) {
        lastError = "Failed to enqueue 3D kernel: " + std::to_string(err);
        return false;
    }
    
    clFinish(queue);
    return true;
}

bool OpenCLRuntime::setKernelArgBuffer(const std::string& kernelName,
                                       int index,
                                       const cl_mem& buffer) {
    auto it = kernels.find(kernelName);
    if (it == kernels.end()) {
        lastError = "Kernel not found: " + kernelName;
        return false;
    }
    
    cl_int err = clSetKernelArg(it->second, index, sizeof(cl_mem), &buffer);
    if (err != CL_SUCCESS) {
        lastError = "Failed to set kernel arg buffer: " + std::to_string(err);
        return false;
    }
    return true;
}

std::string OpenCLRuntime::getDeviceName() const {
    if (!initialized) return "Not initialized";
    char name[256];
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(name), name, nullptr);
    return std::string(name);
}

std::string OpenCLRuntime::getDeviceVendor() const {
    if (!initialized) return "Not initialized";
    char vendor[256];
    clGetDeviceInfo(device, CL_DEVICE_VENDOR, sizeof(vendor), vendor, nullptr);
    return std::string(vendor);
}

size_t OpenCLRuntime::getMaxWorkGroupSize() const {
    if (!initialized) return 0;
    size_t size;
    clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size), &size, nullptr);
    return size;
}

size_t OpenCLRuntime::getMaxComputeUnits() const {
    if (!initialized) return 0;
    cl_uint units;
    clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(units), &units, nullptr);
    return units;
}

size_t OpenCLRuntime::getGlobalMemorySize() const {
    if (!initialized) return 0;
    cl_ulong size;
    clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(size), &size, nullptr);
    return size;
}

size_t OpenCLRuntime::getLocalMemorySize() const {
    if (!initialized) return 0;
    cl_ulong size;
    clGetDeviceInfo(device, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(size), &size, nullptr);
    return size;
}

std::string OpenCLRuntime::getDeviceVersion() const {
    if (!initialized) return "Not initialized";
    char version[256];
    clGetDeviceInfo(device, CL_DEVICE_VERSION, sizeof(version), version, nullptr);
    return std::string(version);
}

std::string OpenCLRuntime::getDriverVersion() const {
    if (!initialized) return "Not initialized";
    char version[256];
    clGetDeviceInfo(device, CL_DRIVER_VERSION, sizeof(version), version, nullptr);
    return std::string(version);
}

const KernelInfo& OpenCLRuntime::getKernelInfo(const std::string& name) const {
    static KernelInfo empty;
    auto it = kernelInfosMap.find(name);
    if (it != kernelInfosMap.end()) {
        return it->second;
    }
    return empty;
}

} // namespace KLang