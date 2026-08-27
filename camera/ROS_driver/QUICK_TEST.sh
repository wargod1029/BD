#!/bin/bash
echo "============================================"
echo "QUICK GPU MEMORY OVERFLOW TEST"
echo "============================================"
echo ""
echo "Current GPU Status:"
nvidia-smi --query-gpu=memory.used,memory.total,utilization.gpu --format=csv,noheader
echo ""
echo "Current Camera Processes:"
ps aux | grep grabImgWithTrigger | grep -v grep | wc -l
echo ""
echo "To test GPU memory overflow:"
echo "1. Kill gpu_burn to free memory: kill 47370"
echo "2. Compile test: nvcc -o test_gpu_memory test_gpu_memory.cpp"
echo "3. Run test: ./test_gpu_memory 800"
echo "4. Watch logs: tail -f ~/.ros/log/latest/rosout.log | grep ERROR"
echo ""
