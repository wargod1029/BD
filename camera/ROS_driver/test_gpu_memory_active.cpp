// Active GPU Memory Pressure Test
// This simulates DYNAMIC allocation/deallocation like NVJPEG does
// Compile: nvcc -o test_gpu_memory_active test_gpu_memory_active.cpp

#include <cuda_runtime.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>

void printGPUMemory() {
    size_t free_byte, total_byte;
    cudaMemGetInfo(&free_byte, &total_byte);
    
    double free_gb = free_byte / 1024.0 / 1024.0 / 1024.0;
    double total_gb = total_byte / 1024.0 / 1024.0 / 1024.0;
    double used_gb = total_gb - free_gb;
    
    std::cout << "GPU Memory: " << used_gb << " GB / " << total_gb << " GB ("
              << (used_gb/total_gb*100) << "% used) - " 
              << free_gb << " GB free" << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "============================================" << std::endl;
    std::cout << "ACTIVE GPU MEMORY PRESSURE TEST" << std::endl;
    std::cout << "Simulates dynamic allocation like NVJPEG" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << std::endl;
    
    // Initial state
    std::cout << "Initial GPU Memory State:" << std::endl;
    printGPUMemory();
    std::cout << std::endl;
    
    // Base allocation (simulating existing camera buffers)
    size_t base_mb = 4000;  // 4 GB base
    if (argc > 1) {
        base_mb = std::atoi(argv[1]);
    }
    
    std::cout << "Allocating " << base_mb << " MB base load..." << std::endl;
    void* base_ptr;
    cudaError_t err = cudaMalloc(&base_ptr, base_mb * 1024 * 1024);
    if (err != cudaSuccess) {
        std::cerr << "Base allocation failed: " << cudaGetErrorString(err) << std::endl;
        return 1;
    }
    
    std::cout << "✓ Base allocated" << std::endl;
    printGPUMemory();
    std::cout << std::endl;
    
    // Now simulate NVJPEG dynamic allocations
    std::cout << "Starting dynamic allocation/deallocation cycles..." << std::endl;
    std::cout << "This simulates what happens during frame encoding." << std::endl;
    std::cout << "Press Ctrl+C to stop..." << std::endl;
    std::cout << std::endl;
    
    int cycle = 0;
    int failures = 0;
    
    try {
        while (true) {
            cycle++;
            
            // Simulate NVJPEG temporary buffer allocation
            // Each camera encodes 2 images per frame (4K + scaled)
            // Try to allocate 50 MB (like NVJPEG would)
            void* temp_ptr;
            size_t temp_size = 50 * 1024 * 1024;
            
            err = cudaMalloc(&temp_ptr, temp_size);
            
            if (err == cudaSuccess) {
                // Success - free it immediately (like NVJPEG does after encoding)
                cudaFree(temp_ptr);
                if (cycle % 100 == 0) {
                    std::cout << "✓ Cycle " << cycle << " - Allocation succeeded" << std::endl;
                    printGPUMemory();
                }
            } else {
                failures++;
                std::cerr << std::endl;
                std::cerr << "✗ CYCLE " << cycle << " - ALLOCATION FAILED!" << std::endl;
                std::cerr << "   Error: " << cudaGetErrorString(err) << std::endl;
                std::cerr << "   This simulates frame grab error 0x80000007" << std::endl;
                std::cerr << "   Failure rate: " << failures << "/" << cycle 
                          << " (" << (failures*100.0/cycle) << "%)" << std::endl;
                printGPUMemory();
                std::cerr << std::endl;
                
                // Clear the error
                cudaGetLastError();
            }
            
            // Simulate frame interval (10 fps = 100ms per frame)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } catch (...) {
        std::cout << std::endl;
        std::cout << "Test interrupted" << std::endl;
    }
    
    // Cleanup
    std::cout << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "TEST SUMMARY" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "Total cycles: " << cycle << std::endl;
    std::cout << "Failures: " << failures << std::endl;
    std::cout << "Failure rate: " << (failures*100.0/cycle) << "%" << std::endl;
    std::cout << std::endl;
    
    cudaFree(base_ptr);
    
    std::cout << "Final GPU Memory State:" << std::endl;
    printGPUMemory();
    std::cout << "Done." << std::endl;
    
    return 0;
}

