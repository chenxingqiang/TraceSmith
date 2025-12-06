#!/usr/bin/env python3
"""
TraceSmith - Cross-Platform Device Utilities

Provides unified device abstraction for running examples across different
GPU platforms (CUDA, Metal/MPS, ROCm, CPU).

Usage:
    from device_utils import DeviceManager, get_device
    
    # Auto-detect best device
    device = get_device()
    
    # Or use DeviceManager for more control
    dm = DeviceManager()
    device = dm.get_torch_device()
    tensor = dm.create_tensor([1, 2, 3])
"""

import os
import sys
from enum import Enum
from typing import Optional, List, Any, Tuple, Dict
from dataclasses import dataclass
import warnings

# Try importing tracesmith
try:
    import tracesmith as ts
    TRACESMITH_AVAILABLE = True
except ImportError:
    TRACESMITH_AVAILABLE = False

# Try importing PyTorch
try:
    import torch
    TORCH_AVAILABLE = True
except ImportError:
    TORCH_AVAILABLE = False


class DeviceType(Enum):
    """Supported device types."""
    CUDA = "cuda"
    MPS = "mps"        # Apple Metal Performance Shaders
    ROCM = "rocm"      # AMD ROCm (uses cuda device in PyTorch)
    CPU = "cpu"
    UNKNOWN = "unknown"


@dataclass
class DeviceInfo:
    """Information about a compute device."""
    device_type: DeviceType
    device_index: int
    name: str
    total_memory: int  # bytes
    compute_capability: Optional[Tuple[int, int]] = None  # CUDA only
    is_available: bool = True
    
    def __str__(self) -> str:
        mem_gb = self.total_memory / (1024 ** 3)
        return f"{self.name} ({self.device_type.value}:{self.device_index}, {mem_gb:.1f} GB)"


class DeviceManager:
    """
    Cross-platform device manager for GPU/CPU operations.
    
    Automatically detects available devices and provides unified interfaces
    for tensor operations, profiling, and benchmarking.
    """
    
    def __init__(self, prefer_device: Optional[str] = None):
        """
        Initialize device manager.
        
        Args:
            prefer_device: Preferred device type ('cuda', 'mps', 'rocm', 'cpu')
                          If None, auto-detects best available device.
        """
        self._devices: List[DeviceInfo] = []
        self._current_device: Optional[DeviceInfo] = None
        self._torch_device: Optional[Any] = None
        
        # Detect all devices
        self._detect_devices()
        
        # Select device
        if prefer_device:
            self._select_device(prefer_device)
        else:
            self._select_best_device()
    
    def _detect_devices(self):
        """Detect all available compute devices."""
        self._devices = []
        
        # Detect CUDA devices
        if TORCH_AVAILABLE and torch.cuda.is_available():
            for i in range(torch.cuda.device_count()):
                props = torch.cuda.get_device_properties(i)
                # Check if it's actually ROCm (HIP)
                is_rocm = hasattr(torch.version, 'hip') and torch.version.hip is not None
                
                self._devices.append(DeviceInfo(
                    device_type=DeviceType.ROCM if is_rocm else DeviceType.CUDA,
                    device_index=i,
                    name=props.name,
                    total_memory=props.total_memory,
                    compute_capability=(props.major, props.minor) if not is_rocm else None,
                    is_available=True
                ))
        
        # Detect MPS (Apple Silicon)
        if TORCH_AVAILABLE and hasattr(torch.backends, 'mps') and torch.backends.mps.is_available():
            # MPS doesn't have detailed device info
            self._devices.append(DeviceInfo(
                device_type=DeviceType.MPS,
                device_index=0,
                name="Apple Silicon GPU",
                total_memory=self._get_mps_memory(),
                is_available=True
            ))
        
        # Always add CPU as fallback
        import platform
        cpu_name = platform.processor() or "CPU"
        self._devices.append(DeviceInfo(
            device_type=DeviceType.CPU,
            device_index=0,
            name=cpu_name,
            total_memory=self._get_system_memory(),
            is_available=True
        ))
    
    def _get_mps_memory(self) -> int:
        """Estimate MPS (Metal) GPU memory."""
        try:
            # On Apple Silicon, GPU shares memory with CPU
            import subprocess
            result = subprocess.run(
                ['sysctl', '-n', 'hw.memsize'],
                capture_output=True, text=True
            )
            total_mem = int(result.stdout.strip())
            # GPU typically can use ~70% of unified memory
            return int(total_mem * 0.7)
        except Exception:
            return 8 * 1024 ** 3  # Default 8GB
    
    def _get_system_memory(self) -> int:
        """Get total system memory."""
        try:
            import psutil
            return psutil.virtual_memory().total
        except ImportError:
            return 16 * 1024 ** 3  # Default 16GB
    
    def _select_device(self, device_type: str):
        """Select a specific device type."""
        device_type = device_type.lower()
        
        for dev in self._devices:
            if dev.device_type.value == device_type and dev.is_available:
                self._current_device = dev
                self._setup_torch_device()
                return
        
        # Fallback to CPU
        warnings.warn(f"Device '{device_type}' not available, falling back to CPU")
        for dev in self._devices:
            if dev.device_type == DeviceType.CPU:
                self._current_device = dev
                self._setup_torch_device()
                return
    
    def _select_best_device(self):
        """Select the best available device (GPU > CPU)."""
        # Priority: CUDA > ROCm > MPS > CPU
        priority = [DeviceType.CUDA, DeviceType.ROCM, DeviceType.MPS, DeviceType.CPU]
        
        for ptype in priority:
            for dev in self._devices:
                if dev.device_type == ptype and dev.is_available:
                    self._current_device = dev
                    self._setup_torch_device()
                    return
    
    def _setup_torch_device(self):
        """Setup PyTorch device object."""
        if not TORCH_AVAILABLE or self._current_device is None:
            self._torch_device = None
            return
        
        dev = self._current_device
        if dev.device_type == DeviceType.CUDA:
            self._torch_device = torch.device(f"cuda:{dev.device_index}")
        elif dev.device_type == DeviceType.ROCM:
            self._torch_device = torch.device(f"cuda:{dev.device_index}")
        elif dev.device_type == DeviceType.MPS:
            self._torch_device = torch.device("mps")
        else:
            self._torch_device = torch.device("cpu")
    
    @property
    def device(self) -> Optional[DeviceInfo]:
        """Get current device info."""
        return self._current_device
    
    @property
    def device_type(self) -> DeviceType:
        """Get current device type."""
        return self._current_device.device_type if self._current_device else DeviceType.CPU
    
    @property
    def torch_device(self) -> Any:
        """Get PyTorch device object."""
        return self._torch_device
    
    @property
    def is_gpu(self) -> bool:
        """Check if current device is a GPU."""
        return self.device_type in (DeviceType.CUDA, DeviceType.ROCM, DeviceType.MPS)
    
    @property
    def is_cuda(self) -> bool:
        """Check if current device is CUDA."""
        return self.device_type == DeviceType.CUDA
    
    @property
    def is_mps(self) -> bool:
        """Check if current device is MPS (Apple Metal)."""
        return self.device_type == DeviceType.MPS
    
    @property
    def is_rocm(self) -> bool:
        """Check if current device is ROCm."""
        return self.device_type == DeviceType.ROCM
    
    def list_devices(self) -> List[DeviceInfo]:
        """List all detected devices."""
        return self._devices.copy()
    
    def get_tracesmith_platform(self) -> Any:
        """Get TraceSmith platform type for current device."""
        if not TRACESMITH_AVAILABLE:
            return None
        
        if self.device_type == DeviceType.CUDA:
            return ts.PlatformType.CUDA
        elif self.device_type == DeviceType.MPS:
            return ts.PlatformType.Metal
        elif self.device_type == DeviceType.ROCM:
            # ROCm uses CUDA interface in TraceSmith (via HIP)
            return ts.PlatformType.CUDA
        else:
            return ts.PlatformType.Unknown
    
    def create_profiler(self) -> Optional[Any]:
        """Create TraceSmith profiler for current device."""
        if not TRACESMITH_AVAILABLE:
            return None
        
        platform = self.get_tracesmith_platform()
        if platform == ts.PlatformType.Unknown:
            return None
        
        return ts.create_profiler(platform)
    
    def synchronize(self):
        """Synchronize device (wait for all operations to complete)."""
        if not TORCH_AVAILABLE:
            return
        
        if self.device_type == DeviceType.CUDA or self.device_type == DeviceType.ROCM:
            torch.cuda.synchronize()
        elif self.device_type == DeviceType.MPS:
            torch.mps.synchronize()
    
    def empty_cache(self):
        """Clear device memory cache."""
        if not TORCH_AVAILABLE:
            return
        
        if self.device_type == DeviceType.CUDA or self.device_type == DeviceType.ROCM:
            torch.cuda.empty_cache()
        elif self.device_type == DeviceType.MPS:
            torch.mps.empty_cache()
    
    def memory_allocated(self) -> int:
        """Get currently allocated memory in bytes."""
        if not TORCH_AVAILABLE:
            return 0
        
        if self.device_type == DeviceType.CUDA or self.device_type == DeviceType.ROCM:
            return torch.cuda.memory_allocated()
        elif self.device_type == DeviceType.MPS:
            return torch.mps.current_allocated_memory()
        return 0
    
    def memory_reserved(self) -> int:
        """Get currently reserved memory in bytes."""
        if not TORCH_AVAILABLE:
            return 0
        
        if self.device_type == DeviceType.CUDA or self.device_type == DeviceType.ROCM:
            return torch.cuda.memory_reserved()
        elif self.device_type == DeviceType.MPS:
            # MPS doesn't have reserved memory concept
            return torch.mps.current_allocated_memory()
        return 0
    
    def create_tensor(self, data, dtype=None) -> Any:
        """Create a tensor on the current device."""
        if not TORCH_AVAILABLE:
            raise RuntimeError("PyTorch not available")
        
        return torch.tensor(data, device=self._torch_device, dtype=dtype)
    
    def randn(self, *shape, dtype=None) -> Any:
        """Create a random tensor on the current device."""
        if not TORCH_AVAILABLE:
            raise RuntimeError("PyTorch not available")
        
        return torch.randn(*shape, device=self._torch_device, dtype=dtype)
    
    def zeros(self, *shape, dtype=None) -> Any:
        """Create a zero tensor on the current device."""
        if not TORCH_AVAILABLE:
            raise RuntimeError("PyTorch not available")
        
        return torch.zeros(*shape, device=self._torch_device, dtype=dtype)
    
    def ones(self, *shape, dtype=None) -> Any:
        """Create a ones tensor on the current device."""
        if not TORCH_AVAILABLE:
            raise RuntimeError("PyTorch not available")
        
        return torch.ones(*shape, device=self._torch_device, dtype=dtype)
    
    def to_device(self, tensor_or_model) -> Any:
        """Move tensor or model to current device."""
        if not TORCH_AVAILABLE:
            raise RuntimeError("PyTorch not available")
        
        return tensor_or_model.to(self._torch_device)
    
    def get_device_name(self) -> str:
        """Get human-readable device name."""
        if self._current_device:
            return str(self._current_device)
        return "Unknown"
    
    def print_info(self):
        """Print device information."""
        print("=" * 60)
        print("Device Information")
        print("=" * 60)
        
        print(f"\nCurrent device: {self.get_device_name()}")
        print(f"Device type: {self.device_type.value}")
        print(f"Is GPU: {self.is_gpu}")
        
        if self._torch_device:
            print(f"PyTorch device: {self._torch_device}")
        
        print(f"\nAll detected devices:")
        for i, dev in enumerate(self._devices):
            marker = " [active]" if dev == self._current_device else ""
            print(f"  {i}. {dev}{marker}")
        
        if self.is_gpu:
            print(f"\nMemory allocated: {self.memory_allocated() / 1024**2:.1f} MB")
            print(f"Memory reserved: {self.memory_reserved() / 1024**2:.1f} MB")


# Singleton instance for convenience
_default_manager: Optional[DeviceManager] = None


def get_device_manager(prefer_device: Optional[str] = None) -> DeviceManager:
    """Get or create the default device manager."""
    global _default_manager
    
    if _default_manager is None or prefer_device is not None:
        _default_manager = DeviceManager(prefer_device)
    
    return _default_manager


def get_device(prefer_device: Optional[str] = None) -> Any:
    """Get PyTorch device object for the best available device."""
    dm = get_device_manager(prefer_device)
    return dm.torch_device


def get_device_type(prefer_device: Optional[str] = None) -> DeviceType:
    """Get the type of the best available device."""
    dm = get_device_manager(prefer_device)
    return dm.device_type


def is_gpu_available() -> bool:
    """Check if any GPU is available."""
    dm = get_device_manager()
    return dm.is_gpu


def synchronize():
    """Synchronize the current device."""
    dm = get_device_manager()
    dm.synchronize()


def print_device_info():
    """Print information about all available devices."""
    dm = get_device_manager()
    dm.print_info()


# =============================================================================
# Test Utilities
# =============================================================================

def skip_if_no_gpu(func):
    """Decorator to skip test if no GPU is available."""
    import functools
    
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        dm = get_device_manager()
        if not dm.is_gpu:
            print(f"SKIPPED: {func.__name__} (no GPU available)")
            return None
        return func(*args, **kwargs)
    
    return wrapper


def skip_if_not_cuda(func):
    """Decorator to skip test if CUDA is not available."""
    import functools
    
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        dm = get_device_manager()
        if not dm.is_cuda:
            print(f"SKIPPED: {func.__name__} (CUDA not available)")
            return None
        return func(*args, **kwargs)
    
    return wrapper


def skip_if_not_mps(func):
    """Decorator to skip test if MPS is not available."""
    import functools
    
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        dm = get_device_manager()
        if not dm.is_mps:
            print(f"SKIPPED: {func.__name__} (MPS not available)")
            return None
        return func(*args, **kwargs)
    
    return wrapper


def run_on_all_devices(func):
    """Decorator to run test on all available devices."""
    import functools
    
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        results = {}
        
        for dev_type in ['cuda', 'mps', 'cpu']:
            try:
                dm = DeviceManager(prefer_device=dev_type)
                if dm.device_type.value == dev_type:
                    print(f"\n--- Running on {dev_type.upper()} ---")
                    result = func(dm, *args, **kwargs)
                    results[dev_type] = result
            except Exception as e:
                print(f"--- {dev_type.upper()}: Error - {e} ---")
                results[dev_type] = None
        
        return results
    
    return wrapper


# =============================================================================
# Benchmark Utilities
# =============================================================================

class BenchmarkTimer:
    """Cross-platform benchmark timer with GPU synchronization."""
    
    def __init__(self, dm: Optional[DeviceManager] = None):
        self.dm = dm or get_device_manager()
        self._start_time = 0
        self._end_time = 0
    
    def __enter__(self):
        self.dm.synchronize()
        import time
        self._start_time = time.perf_counter()
        return self
    
    def __exit__(self, *args):
        self.dm.synchronize()
        import time
        self._end_time = time.perf_counter()
    
    @property
    def elapsed_ms(self) -> float:
        """Get elapsed time in milliseconds."""
        return (self._end_time - self._start_time) * 1000
    
    @property
    def elapsed_s(self) -> float:
        """Get elapsed time in seconds."""
        return self._end_time - self._start_time


def benchmark(func, warmup: int = 3, iterations: int = 10, 
              dm: Optional[DeviceManager] = None) -> Dict[str, float]:
    """
    Benchmark a function with proper GPU synchronization.
    
    Args:
        func: Function to benchmark (no arguments)
        warmup: Number of warmup iterations
        iterations: Number of timed iterations
        dm: Device manager (uses default if None)
    
    Returns:
        Dict with 'mean_ms', 'std_ms', 'min_ms', 'max_ms'
    """
    dm = dm or get_device_manager()
    
    # Warmup
    for _ in range(warmup):
        func()
        dm.synchronize()
    
    # Timed runs
    times = []
    for _ in range(iterations):
        with BenchmarkTimer(dm) as timer:
            func()
        times.append(timer.elapsed_ms)
    
    import statistics
    return {
        'mean_ms': statistics.mean(times),
        'std_ms': statistics.stdev(times) if len(times) > 1 else 0,
        'min_ms': min(times),
        'max_ms': max(times),
        'iterations': iterations
    }


# =============================================================================
# Main - Test Device Detection
# =============================================================================

if __name__ == "__main__":
    print("TraceSmith Device Utilities Test")
    print("=" * 60)
    
    # Test device detection
    dm = DeviceManager()
    dm.print_info()
    
    # Test tensor creation
    if TORCH_AVAILABLE:
        print("\n" + "=" * 60)
        print("Tensor Creation Test")
        print("=" * 60)
        
        x = dm.randn(1000, 1000)
        y = dm.randn(1000, 1000)
        
        print(f"\nCreated tensors on {dm.device_type.value}")
        print(f"x shape: {x.shape}, device: {x.device}")
        
        # Simple benchmark
        def matmul():
            return torch.mm(x, y)
        
        print("\nBenchmarking matrix multiplication (1000x1000)...")
        results = benchmark(matmul, warmup=3, iterations=10, dm=dm)
        print(f"  Mean: {results['mean_ms']:.2f} ms")
        print(f"  Std:  {results['std_ms']:.2f} ms")
        print(f"  Min:  {results['min_ms']:.2f} ms")
        print(f"  Max:  {results['max_ms']:.2f} ms")
    
    print("\n" + "=" * 60)
    print("Test Complete!")
