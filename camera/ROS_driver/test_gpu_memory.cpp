// GPU Memory Overflow Test - Simple C++ version
// Compile: nvcc -o test_gpu_memory test_gpu_memory.cpp
// Run: ./test_gpu_memory <size_in_MB>

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
              << (used_gb/total_gb*100) << "% used)" << std::endl;
    std::cout << "Available: " << free_gb << " GB (" << free_byte/1024/1024 << " MB)" << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "============================================" << std::endl;
    std::cout << "GPU MEMORY OVERFLOW TEST" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << std::endl;
    
    // Initial state
    std::cout << "Initial GPU Memory State:" << std::endl;
    printGPUMemory();
    std::cout << std::endl;
    
    // Determine allocation size
    size_t allocate_mb = 500;  // Default
    if (argc > 1) {
        allocate_mb = std::atoi(argv[1]);
    }
    
    std::cout << "Allocating " << allocate_mb << " MB on GPU..." << std::endl;
    std::cout << "This will simulate GPU memory pressure." << std::endl;
    std::cout << std::endl;
    
    // Allocate in chunks
    std::vector<void*> allocations;
    size_t chunk_mb = 100;
    size_t num_chunks = allocate_mb / chunk_mb;
    
    for (size_t i = 0; i < num_chunks; i++) {
        void* ptr;
        size_t bytes = chunk_mb * 1024 * 1024;
        cudaError_t err = cudaMalloc(&ptr, bytes);
        
        if (err == cudaSuccess) {
            allocations.push_back(ptr);
            std::cout << "✓ Chunk " << (i+1) << "/" << num_chunks 
                      << " allocated (" << chunk_mb << " MB)" << std::endl;
            printGPUMemory();
        } else {
            std::cerr << "✗ ALLOCATION FAILED: " << cudaGetErrorString(err) << std::endl;
            std::cerr << std::endl;
            std::cerr << "⚠️  GPU OUT OF MEMORY!" << std::endl;
            std::cerr << "This is the condition causing camera frame grab errors." << std::endl;
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    std::cout << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "ALLOCATION COMPLETE" << std::endl;
    std::cout << "============================================" << std::endl;
    printGPUMemory();
    std::cout << std::endl;
    std::cout << "Memory is held. Monitor camera behavior now." << std::endl;
    std::cout << "Check for errors in: tail -f ~/.ros/log/latest/rosout.log" << std::endl;
    std::cout << std::endl;
    std::cout << "Press Enter to release memory and exit..." << std::endl;
    std::cin.get();
    
    // Cleanup
    std::cout << std::endl;
    std::cout << "Releasing GPU memory..." << std::endl;
    for (void* ptr : allocations) {
        cudaFree(ptr);
    }
    
    std::cout << "Final GPU Memory State:" << std::endl;
    printGPUMemory();
    std::cout << "Done." << std::endl;
    
    return 0;
}

