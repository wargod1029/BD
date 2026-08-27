# NVJPEG: Software vs Hardware Encoder Clarification

## ❌ **IMPORTANT: NVJPEG is NOT a Hardware Encoder!**

### What is NVJPEG?

**NVJPEG** = **GPU-Accelerated SOFTWARE JPEG Encoder**
- Uses CUDA cores for parallel processing
- Consumes GPU memory for buffers and intermediate data
- Shows up as **GPU compute utilization** (not encoder utilization)

### Verification from `nvidia-smi`:

```bash
$ nvidia-smi dmon -s u
# gpu     sm    mem    enc    dec    jpg    ofa 
# Idx      %      %      %      %      %      % 
    0     31      8      0      0      0      0
```

**Key columns:**
- `sm` (Streaming Multiprocessors/CUDA cores): **31%** ← NVJPEG uses this
- `enc` (Hardware H.264/H.265 encoder): **0%** ← NOT being used
- `jpg` (Hardware JPEG codec): **0%** ← NOT being used

**Conclusion**: Your cameras are using **CUDA cores**, not dedicated hardware encoders!

---

## 🔍 NVIDIA GPU Encoder Types

### 1. **NVJPEG** (What you're using)
- **Type**: Software encoder running on CUDA cores
- **Use case**: JPEG compression
- **Pros**: Flexible, good quality
- **Cons**: Consumes GPU memory (~810 MB per camera) and compute

### 2. **NVENC** (Hardware Video Encoder)
- **Type**: Dedicated hardware encoder chip
- **Use case**: H.264, H.265 video encoding
- **Pros**: Minimal GPU memory/compute impact
- **Cons**: Video only, not JPEG; fixed quality presets

### 3. **Hardware JPEG Encoder** (Exists but rarely used)
- **Type**: Dedicated JPEG encoder hardware
- **Use case**: JPEG compression
- **Availability**: RTX 3080 has it, but most software doesn't use it
- **Note**: Would show in `jpg` column of nvidia-smi

---

## 📊 Your Current Setup Analysis

### Per-Camera GPU Usage:
```
Each camera process: ~810 MB GPU memory
6 cameras total:     ~4,900 MB (4.9 GB)
gpu_burn process:    ~4,300 MB (4.3 GB)
System (Xorg):       ~80 MB
─────────────────────────────────────
TOTAL:               9,280 MB / 10,240 MB (90.6%)
AVAILABLE:           960 MB (9.4%)
```

### What Each Camera Does on GPU:

From `grab_trigger.cpp`:
```cpp
// Per camera allocations:
cudaMalloc(&bayerGPU_, max_buffer_size);        // ~8-25 MB
cudaMalloc(&rgbGPU_, max_buffer_size * 3);      // ~25-75 MB  
cudaMalloc(&undistortedGPU_, max_buffer_size * 3); // ~25-75 MB

// Plus NVJPEG encoder state (2 instances per camera):
nvjpegEncoderState_t nvjpeg_encoder_state;      // ~300 MB
nvjpegEncoderState_t nvjpeg_encoder_state_4k;   // ~300 MB
```

**Total per camera: ~810 MB**

### Per-Frame GPU Operations:
1. `cudaMemcpy` (Bayer data to GPU)
2. `cudaBayerToRGB` (Bayer → RGB conversion)
3. `cv::cuda::remap` (lens undistortion)
4. `cv::cuda::resize` (scaling for different outputs)
5. `nvjpegEncodeImage` (JPEG compression) × 2 (4K + scaled)
6. `cudaStreamSynchronize` (wait for completion)

Each operation needs temporary GPU memory!

---

## 🔥 Why You're Getting Frame Grab Errors

### The Cascade:

1. **Initial State**: 6 cameras use ~4.9 GB
2. **Normal Operation**: Works fine with 5 GB free
3. **Detection Event**: Object detection triggers
   - YOLOv8 TensorRT inference allocates GPU memory
   - Input image preprocessing needs memory
   - Output buffers need memory
   - **Suddenly needs +500-1000 MB**
4. **GPU Memory Overflow**: Total > 10.2 GB
5. **NVJPEG Failure**: 
   ```cpp
   nvjpegEncodeImage() → NVJPEG_STATUS_ALLOCATOR_FAILURE
   ```
6. **Camera Timeout**: Frame grab waits for NVJPEG → timeout
7. **Error 0x80000007**: "Too many consecutive errors"
8. **Camera Reconnect**: Attempts to recover

### Why DA5324645 Shows FPS Degradation:

- Camera doesn't crash completely
- NVJPEG encoder becomes memory-starved
- Processing slows down (5-5.6 fps instead of 10 fps)
- System is throttling due to memory pressure

---

## 🧪 How to Test GPU Memory Overflow

### Option 1: Python Script (Easiest)
```bash
# Install CuPy if not installed
pip3 install cupy-cuda12x

# Run test
cd /home/kodifly/isds_ws
./test_gpu_memory_overflow.py
```

### Option 2: C++ Program (More Control)
```bash
# Compile
cd /home/kodifly/isds_ws
nvcc -o test_gpu_memory test_gpu_memory.cpp

# Run (allocate 500 MB)
./test_gpu_memory 500

# Or allocate more to trigger overflow
./test_gpu_memory 900
```

### Option 3: Watch Real-Time
```bash
# Terminal 1: Monitor GPU
watch -n 0.5 nvidia-smi

# Terminal 2: Monitor camera errors
tail -f ~/.ros/log/latest/rosout.log | grep -E "error|Error|ERROR|FAIL|Frame grab"

# Terminal 3: Run overflow test
./test_gpu_memory 800
```

### Expected Behavior:
- ✓ At 500 MB allocation: Cameras may start showing warnings
- ⚠️ At 700-800 MB allocation: Frame grab errors begin
- ❌ At 900+ MB allocation: Cameras fail and reconnect

---

## 💡 Solutions

### Immediate (Kill gpu_burn):
```bash
# Free 4.3 GB immediately!
kill 47370
nvidia-smi  # Verify memory freed
```

### Short-term (Reduce per-camera memory):

**Option A**: Lower JPEG quality
```cpp
// In grab_trigger.cpp line 501, 513:
nvjpegEncoderParamsSetQuality(nvjpeg_encoder_params, 60, stream);  // was 80
nvjpegEncoderParamsSetQuality(nvjpeg_encoder_params_4k, 50, stream_4k);  // was 70
```

**Option B**: Disable 4K output for some cameras
- Removes one NVJPEG encoder instance per camera
- Saves ~300-400 MB per camera

**Option C**: Reduce resolution
```cpp
// Reduce max_buffer_size allocation
// Or crop more aggressively
```

### Long-term (Use actual hardware encoder):

**Option 1**: Switch to NVENC (H.264/H.265)
- Use `libnvenc` or FFmpeg's nvenc
- Minimal GPU memory impact
- But need to decode on receiving end

**Option 2**: Use camera's built-in JPEG encoder
- Cameras likely have hardware JPEG encoder
- Offloads all processing from GPU
- May need MVS SDK configuration change

**Option 3**: Upgrade GPU
- RTX 3090: 24 GB (2.4x more memory)
- RTX A6000: 48 GB (4.8x more memory)
- Eliminate memory constraints

---

## 📝 Summary

| Feature | NVJPEG (Current) | NVENC (Alternative) | Camera HW |
|---------|------------------|---------------------|-----------|
| Type | Software on CUDA | Hardware encoder | Hardware |
| Memory | High (~810MB/cam) | Very Low (~50MB) | Zero |
| GPU Compute | High (31%) | Minimal (<5%) | Zero |
| Format | JPEG | H.264/H.265 | JPEG |
| Quality | Excellent | Good | Excellent |
| Flexibility | High | Medium | Low |

**Your issue**: NVJPEG + gpu_burn + detection = **GPU memory overflow** → **Error 0x80000007**

**Best fix**: Remove gpu_burn, optimize NVJPEG settings, or switch to hardware JPEG in cameras.

