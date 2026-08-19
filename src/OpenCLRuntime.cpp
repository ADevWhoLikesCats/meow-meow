// src/OpenCLRuntime.cpp - OpenCL Runtime Implementation
// Pushpita Catol Approved ✅

#include "OpenCLRuntime.h"
#include <iostream>
#include <sstream>

namespace KLang {

OpenCLRuntime::OpenCLRuntime() {
    initialized = false;
}

OpenCLRuntime::~OpenCLRuntime() {
    // Cleanup
}

bool OpenCLRuntime::initialize(bool preferGPU) {
    try {
        std::vector<cl::Platform> platforms;
        cl::Platform::get(&platforms);
        
        if (platforms.empty()) {
            lastError = "No OpenCL platforms found";
            return false;
        }
        
        // Select device
        if (!selectDevice(preferGPU)) {
            return false;
        }
        
        // Create context and command queue
        context = cl::Context(device);
        queue = cl::CommandQueue(context, device);
        
        initialized = true;
        return true;
        
    } catch (const cl::Error& e) {
        lastError = "OpenCL error: " + std::string(e.what()) + 
                    " (code: " + std::to_string(e.err()) + ")";
        return false;
    } catch (const std::exception& e) {
        lastError = "Error: " + std::string(e.what());
        return false;
    }
}

bool OpenCLRuntime::selectDevice(bool preferGPU) {
    try {
        std::vector<cl::Platform> platforms;
        cl::Platform::get(&platforms);
        
        for (auto& platform : platforms) {
            std::vector<cl::Device> devices;
            
            // Try GPU first if preferred
            if (preferGPU) {
                platform.getDevices(CL_DEVICE_TYPE_GPU, &devices);
                if (!devices.empty()) {
                    device = devices[0];
                    return true;
                }
            }
            
            // Fallback to CPU
            platform.getDevices(CL_DEVICE_TYPE_CPU, &devices);
            if (!devices.empty()) {
                device = devices[0];
                return true;
            }
        }
        
        lastError = "No suitable OpenCL device found";
        return false;
        
    } catch (const cl::Error& e) {
        lastError = "Failed to select device: " + std::string(e.what());
        return false;
    }
}

bool OpenCLRuntime::loadSPIRV(const std::vector<unsigned char>& spirvBinary,
                              const std::vector<KernelInfo>& kernelInfos) {
    if (!initialized) {
        lastError = "Runtime not initialized";
        return false;
    }
    
    try {
        cl::Program::Binaries binaries;
        // FIXED: Use cl::Program::Binary constructor
        binaries.push_back({spirvBinary.data(), spirvBinary.size()});
        
        cl_int err;
        program = cl::Program(context, {device}, binaries, nullptr, &err);
        if (err != CL_SUCCESS) {
            lastError = "Failed to create program from SPIR-V: " + std::to_string(err);
            return false;
        }
        
        // Build program
        err = program.build({device});
        if (err != CL_SUCCESS) {
            std::string buildLog = program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device);
            lastError = "Build failed:\n" + buildLog;
            return false;
        }
        
        // Create kernels
        for (const auto& info : kernelInfos) {
            cl::Kernel kernel(program, info.name.c_str(), &err);
            if (err != CL_SUCCESS) {
                lastError = "Failed to create kernel: " + info.name;
                return false;
            }
            kernels[info.name] = kernel;
        }
        
        // Store kernel info
        for (const auto& info : kernelInfos) {
            kernelInfos[info.name] = info;   // FIXED: use insert instead of operator[]
        }
        
        return true;
        
    } catch (const cl::Error& e) {
        lastError = "OpenCL error: " + std::string(e.what());
        return false;
    }
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
    
    try {
        cl::NDRange global(globalSize);
        cl::NDRange local(localSize);
        
        cl_int err = queue.enqueueNDRangeKernel(it->second, 
                                                cl::NullRange, 
                                                global, 
                                                local);
        if (err != CL_SUCCESS) {
            lastError = "Failed to enqueue kernel: " + std::to_string(err);
            return false;
        }
        
        queue.finish();
        return true;
        
    } catch (const cl::Error& e) {
        lastError = "OpenCL error: " + std::string(e.what());
        return false;
    }
}

bool OpenCLRuntime::runKernel2D(const std::string& kernelName,
                                size_t globalX, size_t globalY,
                                size_t localX, size_t localY) {
    auto it = kernels.find(kernelName);
    if (it == kernels.end()) {
        lastError = "Kernel not found: " + kernelName;
        return false;
    }
    
    try {
        cl::NDRange global(globalX, globalY);
        cl::NDRange local(localX, localY);
        
        cl_int err = queue.enqueueNDRangeKernel(it->second,
                                                cl::NullRange,
                                                global,
                                                local);
        if (err != CL_SUCCESS) {
            lastError = "Failed to enqueue 2D kernel: " + std::to_string(err);
            return false;
        }
        
        queue.finish();
        return true;
        
    } catch (const cl::Error& e) {
        lastError = "OpenCL error: " + std::string(e.what());
        return false;
    }
}

bool OpenCLRuntime::runKernel3D(const std::string& kernelName,
                                size_t globalX, size_t globalY, size_t globalZ,
                                size_t localX, size_t localY, size_t localZ) {
    auto it = kernels.find(kernelName);
    if (it == kernels.end()) {
        lastError = "Kernel not found: " + kernelName;
        return false;
    }
    
    try {
        cl::NDRange global(globalX, globalY, globalZ);
        cl::NDRange local(localX, localY, localZ);
        
        cl_int err = queue.enqueueNDRangeKernel(it->second,
                                                cl::NullRange,
                                                global,
                                                local);
        if (err != CL_SUCCESS) {
            lastError = "Failed to enqueue 3D kernel: " + std::to_string(err);
            return false;
        }
        
        queue.finish();
        return true;
        
    } catch (const cl::Error& e) {
        lastError = "OpenCL error: " + std::string(e.what());
        return false;
    }
}

bool OpenCLRuntime::setKernelArgBuffer(const std::string& kernelName,
                                       int index,
                                       const cl::Buffer& buffer) {
    auto it = kernels.find(kernelName);
    if (it == kernels.end()) {
        lastError = "Kernel not found: " + kernelName;
        return false;
    }
    
    it->second.setArg(index, buffer);
    return true;
}

std::string OpenCLRuntime::getDeviceName() const {
    if (!initialized) return "Not initialized";
    return device.getInfo<CL_DEVICE_NAME>();
}

std::string OpenCLRuntime::getDeviceVendor() const {
    if (!initialized) return "Not initialized";
    return device.getInfo<CL_DEVICE_VENDOR>();
}

size_t OpenCLRuntime::getMaxWorkGroupSize() const {
    if (!initialized) return 0;
    return device.getInfo<CL_DEVICE_MAX_WORK_GROUP_SIZE>();
}

size_t OpenCLRuntime::getMaxComputeUnits() const {
    if (!initialized) return 0;
    return device.getInfo<CL_DEVICE_MAX_COMPUTE_UNITS>();
}

size_t OpenCLRuntime::getGlobalMemorySize() const {
    if (!initialized) return 0;
    return device.getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>();
}

size_t OpenCLRuntime::getLocalMemorySize() const {
    if (!initialized) return 0;
    return device.getInfo<CL_DEVICE_LOCAL_MEM_SIZE>();
}

std::string OpenCLRuntime::getDeviceVersion() const {
    if (!initialized) return "Not initialized";
    return device.getInfo<CL_DEVICE_VERSION>();
}

std::string OpenCLRuntime::getDriverVersion() const {
    if (!initialized) return "Not initialized";
    return device.getInfo<CL_DRIVER_VERSION>();
}

const KernelInfo& OpenCLRuntime::getKernelInfo(const std::string& name) const {
    static KernelInfo empty;
    auto it = kernelInfos.find(name);
    if (it != kernelInfos.end()) {
        return it->second;
    }
    return empty;
}

} // namespace KLang