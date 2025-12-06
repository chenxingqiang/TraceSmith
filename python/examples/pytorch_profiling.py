#!/usr/bin/env python3
"""
TraceSmith Example - PyTorch Model Profiling

Demonstrates how to profile PyTorch models with TraceSmith:
- Capture GPU kernels during model inference/training
- Correlate kernels with PyTorch operations
- Analyze performance bottlenecks
- Export for visualization in Perfetto

Requirements:
    pip install torch torchvision
"""

import tracesmith as ts
import time
from contextlib import contextmanager
from typing import List, Optional, Dict, Any
from dataclasses import dataclass

# Try to import PyTorch
try:
    import torch
    import torch.nn as nn
    import torch.nn.functional as F
    TORCH_AVAILABLE = True
except ImportError:
    TORCH_AVAILABLE = False
    print("PyTorch not available. Install with: pip install torch torchvision")


@dataclass
class ProfileResult:
    """Result of a profiling session."""
    events: List[ts.TraceEvent]
    timeline: ts.Timeline
    duration_ms: float
    kernel_count: int
    memory_events: int


class TorchProfiler:
    """PyTorch model profiler using TraceSmith."""

    def __init__(self, buffer_size: int = 500000):
        self.platform = ts.detect_platform()
        self.profiler = None
        self.events: List[ts.TraceEvent] = []
        self.is_capturing = False
        self.buffer_size = buffer_size

        if self.platform == ts.PlatformType.Unknown:
            print("Warning: No GPU detected. Profiling will be limited.")

    def initialize(self) -> bool:
        """Initialize the profiler."""
        if self.platform == ts.PlatformType.Unknown:
            return False

        self.profiler = ts.create_profiler(self.platform)
        
        config = ts.ProfilerConfig()
        config.buffer_size = self.buffer_size
        config.capture_kernels = True
        config.capture_memcpy = True
        config.capture_memset = True
        config.capture_alloc = True
        config.capture_sync = True

        return self.profiler.initialize(config)

    def start(self):
        """Start capturing GPU events."""
        if self.profiler and not self.is_capturing:
            self.events.clear()
            self.profiler.start_capture()
            self.is_capturing = True

    def stop(self) -> List[ts.TraceEvent]:
        """Stop capturing and return events."""
        if self.profiler and self.is_capturing:
            self.profiler.stop_capture()
            self.events = self.profiler.get_events()
            self.is_capturing = False
        return self.events

    def finalize(self):
        """Cleanup resources."""
        if self.profiler:
            self.profiler.finalize()
            self.profiler = None

    @contextmanager
    def profile(self, sync_device: bool = True):
        """Context manager for profiling a code block.
        
        Args:
            sync_device: Whether to synchronize device before and after profiling
        
        Usage:
            with profiler.profile():
                model(input)
        """
        if TORCH_AVAILABLE and sync_device:
            self._sync_device()

        self.start()
        start_time = time.perf_counter()

        try:
            yield self
        finally:
            if TORCH_AVAILABLE and sync_device:
                self._sync_device()

            end_time = time.perf_counter()
            self.stop()
            self._last_duration = (end_time - start_time) * 1000  # ms
    
    def _sync_device(self):
        """Synchronize device (CUDA/MPS/ROCm)."""
        if not TORCH_AVAILABLE:
            return
        if torch.cuda.is_available():
            torch.cuda.synchronize()
        elif hasattr(torch.backends, 'mps') and torch.backends.mps.is_available():
            torch.mps.synchronize()

    def analyze(self) -> ProfileResult:
        """Analyze captured events."""
        timeline = ts.build_timeline(self.events)
        
        kernel_count = sum(1 for e in self.events 
                          if e.type == ts.EventType.KernelLaunch)
        memory_events = sum(1 for e in self.events 
                           if e.type in [ts.EventType.MemcpyH2D, 
                                         ts.EventType.MemcpyD2H,
                                         ts.EventType.MemcpyD2D,
                                         ts.EventType.MemAlloc,
                                         ts.EventType.MemFree])

        return ProfileResult(
            events=self.events,
            timeline=timeline,
            duration_ms=getattr(self, '_last_duration', 0),
            kernel_count=kernel_count,
            memory_events=memory_events
        )

    def print_summary(self):
        """Print profiling summary."""
        result = self.analyze()

        print("\n" + "=" * 70)
        print("PYTORCH PROFILING SUMMARY")
        print("=" * 70)
        print(f"Total Events:      {len(result.events)}")
        print(f"Kernel Launches:   {result.kernel_count}")
        print(f"Memory Operations: {result.memory_events}")
        print(f"Wall Clock Time:   {result.duration_ms:.2f} ms")
        print(f"GPU Duration:      {result.timeline.total_duration / 1e6:.2f} ms")
        print(f"GPU Utilization:   {result.timeline.gpu_utilization * 100:.1f}%")
        print(f"Max Concurrent:    {result.timeline.max_concurrent_ops}")
        print("=" * 70)

        # Top kernels by time
        kernel_times: Dict[str, float] = {}
        kernel_counts: Dict[str, int] = {}

        for event in self.events:
            if event.type == ts.EventType.KernelLaunch:
                name = event.name
                kernel_times[name] = kernel_times.get(name, 0) + event.duration
                kernel_counts[name] = kernel_counts.get(name, 0) + 1

        if kernel_times:
            print("\nTop 10 Kernels by Total Time:")
            print("-" * 70)
            print(f"{'Kernel Name':<45} {'Count':>8} {'Total(ms)':>12}")
            print("-" * 70)

            sorted_kernels = sorted(kernel_times.items(), 
                                   key=lambda x: x[1], reverse=True)
            for name, total_ns in sorted_kernels[:10]:
                display_name = name[:43] + ".." if len(name) > 45 else name
                print(f"{display_name:<45} {kernel_counts[name]:>8} "
                      f"{total_ns / 1e6:>12.3f}")

    def export_perfetto(self, filename: str) -> bool:
        """Export trace to Perfetto format."""
        return ts.export_perfetto(self.events, filename)


# Example neural network models

class SimpleCNN(nn.Module):
    """Simple CNN for demonstration."""

    def __init__(self, num_classes: int = 10):
        super().__init__()
        self.conv1 = nn.Conv2d(3, 32, 3, padding=1)
        self.conv2 = nn.Conv2d(32, 64, 3, padding=1)
        self.conv3 = nn.Conv2d(64, 128, 3, padding=1)
        self.pool = nn.MaxPool2d(2, 2)
        self.fc1 = nn.Linear(128 * 4 * 4, 512)
        self.fc2 = nn.Linear(512, num_classes)
        self.dropout = nn.Dropout(0.5)

    def forward(self, x):
        x = self.pool(F.relu(self.conv1(x)))
        x = self.pool(F.relu(self.conv2(x)))
        x = self.pool(F.relu(self.conv3(x)))
        x = x.view(-1, 128 * 4 * 4)
        x = F.relu(self.fc1(x))
        x = self.dropout(x)
        x = self.fc2(x)
        return x


class TransformerBlock(nn.Module):
    """Transformer block for demonstration."""

    def __init__(self, d_model: int = 512, nhead: int = 8, dim_ff: int = 2048):
        super().__init__()
        self.self_attn = nn.MultiheadAttention(d_model, nhead, batch_first=True)
        self.norm1 = nn.LayerNorm(d_model)
        self.norm2 = nn.LayerNorm(d_model)
        self.ff = nn.Sequential(
            nn.Linear(d_model, dim_ff),
            nn.GELU(),
            nn.Linear(dim_ff, d_model)
        )

    def forward(self, x):
        # Self-attention
        attn_out, _ = self.self_attn(x, x, x)
        x = self.norm1(x + attn_out)
        # Feed-forward
        ff_out = self.ff(x)
        x = self.norm2(x + ff_out)
        return x


class SimpleTransformer(nn.Module):
    """Simple Transformer model for demonstration."""

    def __init__(self, vocab_size: int = 30000, d_model: int = 512, 
                 nhead: int = 8, num_layers: int = 6, num_classes: int = 10):
        super().__init__()
        self.embedding = nn.Embedding(vocab_size, d_model)
        self.pos_embedding = nn.Parameter(torch.randn(1, 512, d_model))
        self.layers = nn.ModuleList([
            TransformerBlock(d_model, nhead) for _ in range(num_layers)
        ])
        self.classifier = nn.Linear(d_model, num_classes)

    def forward(self, x):
        # x: (batch, seq_len)
        x = self.embedding(x)
        x = x + self.pos_embedding[:, :x.size(1)]
        for layer in self.layers:
            x = layer(x)
        # Take [CLS] token (first position)
        x = x[:, 0]
        return self.classifier(x)


def profile_inference(model: nn.Module, input_data: torch.Tensor, 
                     num_iterations: int = 10, warmup: int = 3) -> TorchProfiler:
    """Profile model inference.
    
    Args:
        model: PyTorch model
        input_data: Input tensor
        num_iterations: Number of inference iterations to profile
        warmup: Number of warmup iterations
    
    Returns:
        TorchProfiler with captured events
    """
    profiler = TorchProfiler()
    
    if not profiler.initialize():
        print("Could not initialize profiler")
        return profiler

    device = next(model.parameters()).device
    input_data = input_data.to(device)

    # Warmup
    print(f"Running {warmup} warmup iterations...")
    with torch.no_grad():
        for _ in range(warmup):
            _ = model(input_data)
            if torch.cuda.is_available():
                torch.cuda.synchronize()

    # Profile
    print(f"Profiling {num_iterations} iterations...")
    with profiler.profile():
        with torch.no_grad():
            for _ in range(num_iterations):
                _ = model(input_data)

    return profiler


def profile_training_step(model: nn.Module, input_data: torch.Tensor,
                         target: torch.Tensor, optimizer: torch.optim.Optimizer,
                         criterion: nn.Module = None) -> TorchProfiler:
    """Profile a single training step.
    
    Args:
        model: PyTorch model
        input_data: Input tensor
        target: Target tensor
        optimizer: PyTorch optimizer
        criterion: Loss function (default: CrossEntropyLoss)
    
    Returns:
        TorchProfiler with captured events
    """
    profiler = TorchProfiler()
    
    if not profiler.initialize():
        print("Could not initialize profiler")
        return profiler

    if criterion is None:
        criterion = nn.CrossEntropyLoss()

    device = next(model.parameters()).device
    input_data = input_data.to(device)
    target = target.to(device)

    # Warmup
    print("Running warmup...")
    model.train()
    for _ in range(3):
        optimizer.zero_grad()
        output = model(input_data)
        loss = criterion(output, target)
        loss.backward()
        optimizer.step()
        if torch.cuda.is_available():
            torch.cuda.synchronize()

    # Profile one training step
    print("Profiling training step...")
    with profiler.profile():
        optimizer.zero_grad()
        output = model(input_data)
        loss = criterion(output, target)
        loss.backward()
        optimizer.step()

    return profiler


def main(device_preference: str = None):
    print("╔═══════════════════════════════════════════════════════════╗")
    print("║  TraceSmith - PyTorch Model Profiling                     ║")
    print("╚═══════════════════════════════════════════════════════════╝\n")

    if not TORCH_AVAILABLE:
        print("PyTorch is required for this example.")
        print("Install with: pip install torch torchvision")
        return

    # Import device utilities if available
    try:
        from device_utils import DeviceManager
        dm = DeviceManager(prefer_device=device_preference)
        device = dm.torch_device
        print(f"Device: {dm.get_device_name()}")
    except ImportError:
        dm = None
        # Fallback device detection
        if device_preference == "mps" and hasattr(torch.backends, 'mps') and torch.backends.mps.is_available():
            device = torch.device("mps")
            print(f"Device: Apple Silicon GPU (MPS)")
        elif device_preference == "cuda" or (device_preference is None and torch.cuda.is_available()):
            device = torch.device("cuda")
            print(f"Device: {torch.cuda.get_device_name(0)}")
        else:
            device = torch.device("cpu")
            print("Device: CPU (profiling will be limited)")

    # Check GPU availability
    platform = ts.detect_platform()
    print(f"TraceSmith Platform: {ts.platform_type_to_string(platform)}")

    print("\n" + "-" * 60)
    print("Example 1: CNN Inference Profiling")
    print("-" * 60)

    # Create CNN model
    cnn = SimpleCNN(num_classes=10).to(device)
    cnn.eval()

    # Create sample input (batch of 32 images, 32x32, RGB)
    batch_size = 32
    input_images = torch.randn(batch_size, 3, 32, 32)

    # Profile inference
    profiler = profile_inference(cnn, input_images, num_iterations=20, warmup=5)
    profiler.print_summary()

    # Export trace
    if profiler.export_perfetto("cnn_inference_trace.json"):
        print("\n✓ Exported: cnn_inference_trace.json")

    print("\n" + "-" * 60)
    print("Example 2: CNN Training Step Profiling")
    print("-" * 60)

    # Setup training
    cnn.train()
    optimizer = torch.optim.Adam(cnn.parameters(), lr=0.001)
    target = torch.randint(0, 10, (batch_size,))

    # Profile training step
    profiler = profile_training_step(cnn, input_images, target, optimizer)
    profiler.print_summary()

    if profiler.export_perfetto("cnn_training_trace.json"):
        print("\n✓ Exported: cnn_training_trace.json")

    print("\n" + "-" * 60)
    print("Example 3: Transformer Inference Profiling")
    print("-" * 60)

    # Create Transformer model
    transformer = SimpleTransformer(
        vocab_size=30000,
        d_model=512,
        nhead=8,
        num_layers=6,
        num_classes=10
    ).to(device)
    transformer.eval()

    # Create sample input (batch of 16, sequence length 128)
    batch_size = 16
    seq_len = 128
    input_tokens = torch.randint(0, 30000, (batch_size, seq_len))

    # Profile inference
    profiler = profile_inference(transformer, input_tokens, num_iterations=10, warmup=3)
    profiler.print_summary()

    if profiler.export_perfetto("transformer_inference_trace.json"):
        print("\n✓ Exported: transformer_inference_trace.json")

    # Cleanup
    profiler.finalize()

    print("\n" + "=" * 60)
    print("Profiling Complete!")
    print("\nGenerated trace files:")
    print("  - cnn_inference_trace.json")
    print("  - cnn_training_trace.json")
    print("  - transformer_inference_trace.json")
    print("\nView traces at: https://ui.perfetto.dev/")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="PyTorch Model Profiling Example")
    parser.add_argument(
        "--device", "-d",
        choices=["cuda", "mps", "rocm", "cpu"],
        default=None,
        help="Preferred device (default: auto-detect)"
    )
    args = parser.parse_args()
    main(device_preference=args.device)
