#!/usr/bin/env python3
"""
GPU Memory Overflow Test Script
This script allocates GPU memory in increments to simulate overflow conditions
and test how the camera system responds.
"""

import cupy as cp
import time
import sys
import subprocess

def get_gpu_memory_info():
    """Get current GPU memory usage"""
    mempool = cp.get_default_memory_pool()
    used = mempool.used_bytes() / (1024**3)  # GB
    total = mempool.total_bytes() / (1024**3)  # GB
    
    # Also get nvidia-smi info
    try:
        result = subprocess.check_output(
            ['nvidia-smi', '--query-gpu=memory.used,memory.total', '--format=csv,noheader,nounits'],
            encoding='utf-8'
        )
        mem_used, mem_total = map(float, result.strip().split(','))
        mem_used_gb = mem_used / 1024
        mem_total_gb = mem_total / 1024
        return mem_used_gb, mem_total_gb
    except:
        return used, 10.0  # fallback

def allocate_gpu_memory(size_mb, description=""):
    """Allocate a chunk of GPU memory"""
    try:
        # Allocate array on GPU
        size_elements = int(size_mb * 1024 * 1024 / 4)  # float32 = 4 bytes
        arr = cp.zeros(size_elements, dtype=cp.float32)
        
        used, total = get_gpu_memory_info()
        print(f"✓ Allocated {size_mb} MB {description}")
        print(f"  GPU Memory: {used:.2f} GB / {total:.2f} GB ({used/total*100:.1f}%)")
        return arr
    except Exception as e:
        print(f"✗ FAILED to allocate {size_mb} MB: {e}")
        return None

def main():
    print("=" * 70)
    print("GPU MEMORY OVERFLOW TEST")
    print("=" * 70)
    print()
    
    # Check initial state
    used, total = get_gpu_memory_info()
    available = total - used
    print(f"Initial GPU Memory: {used:.2f} GB / {total:.2f} GB")
    print(f"Available: {available:.2f} GB ({available*1024:.0f} MB)")
    print()
    
    # Ask user how much to allocate
    print("Current camera processes are using ~4.9 GB")
    print("gpu_burn is using ~4.3 GB")
    print(f"You have ~{available*1024:.0f} MB available")
    print()
    
    try:
        target_mb = input("How many MB to allocate? (default: 500): ").strip()
        target_mb = int(target_mb) if target_mb else 500
    except:
        target_mb = 500
    
    print()
    print(f"Will allocate {target_mb} MB in {target_mb//100} chunks of 100 MB...")
    print("Press Ctrl+C to stop")
    print()
    
    allocations = []
    chunk_size = 100  # MB per chunk
    
    try:
        for i in range(target_mb // chunk_size):
            arr = allocate_gpu_memory(chunk_size, f"(chunk {i+1}/{target_mb//chunk_size})")
            if arr is not None:
                allocations.append(arr)
            else:
                print()
                print("⚠️  GPU MEMORY ALLOCATION FAILED!")
                print("This simulates the condition causing camera errors.")
                break
            
            time.sleep(0.5)  # Pause between allocations
        
        print()
        print("=" * 70)
        print("ALLOCATION COMPLETE")
        print("=" * 70)
        used, total = get_gpu_memory_info()
        print(f"Final GPU Memory: {used:.2f} GB / {total:.2f} GB ({used/total*100:.1f}%)")
        print()
        print("Now check camera behavior with this GPU memory pressure.")
        print("Watch for frame grab errors in camera logs.")
        print()
        print("Press Enter to release memory and exit...")
        input()
        
    except KeyboardInterrupt:
        print()
        print("Interrupted by user")
    
    finally:
        print()
        print("Releasing GPU memory...")
        allocations.clear()
        mempool = cp.get_default_memory_pool()
        mempool.free_all_blocks()
        used, total = get_gpu_memory_info()
        print(f"GPU Memory after cleanup: {used:.2f} GB / {total:.2f} GB")
        print("Done.")

if __name__ == "__main__":
    try:
        import cupy as cp
        main()
    except ImportError:
        print("ERROR: CuPy not installed!")
        print()
        print("To install CuPy:")
        print("  pip3 install cupy-cuda12x")
        print("  (or cupy-cuda11x depending on your CUDA version)")
        sys.exit(1)

