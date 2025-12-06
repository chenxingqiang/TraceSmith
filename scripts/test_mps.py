#!/usr/bin/env python3
"""Simple MPS (Metal Performance Shaders) test for GPU profiling."""

import torch
import time

def main():
    print("=" * 60)
    print("TraceSmith MPS (Metal) Test")
    print("=" * 60)
    
    # Check device
    if torch.backends.mps.is_available():
        device = torch.device("mps")
        print(f"✓ Using MPS (Metal) GPU")
    else:
        device = torch.device("cpu")
        print("⚠ MPS not available, using CPU")
    
    print(f"PyTorch: {torch.__version__}")
    print()
    
    # Matrix multiplication benchmark
    print("Running matrix multiplication benchmark...")
    sizes = [512, 1024, 2048]
    
    for size in sizes:
        # Create tensors
        a = torch.randn(size, size, device=device)
        b = torch.randn(size, size, device=device)
        
        # Warmup
        for _ in range(3):
            _ = torch.matmul(a, b)
        
        if device.type == "mps":
            torch.mps.synchronize()
        
        # Benchmark
        start = time.perf_counter()
        iterations = 10
        for _ in range(iterations):
            c = torch.matmul(a, b)
        
        if device.type == "mps":
            torch.mps.synchronize()
        
        elapsed = time.perf_counter() - start
        avg_ms = (elapsed / iterations) * 1000
        gflops = (2 * size ** 3) / (avg_ms / 1000) / 1e9
        
        print(f"  {size}x{size} matmul: {avg_ms:.2f} ms ({gflops:.1f} GFLOPS)")
    
    print()
    print("=" * 60)
    print("Test Complete!")
    print("=" * 60)

if __name__ == "__main__":
    main()
