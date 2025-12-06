#!/usr/bin/env python3
"""
TraceSmith Example - PyTorch Hooks-Based Layer Profiling

Demonstrates advanced profiling techniques:
- Using PyTorch hooks to correlate GPU kernels with layers
- Layer-by-layer timing breakdown
- Memory tracking per layer
- Automatic bottleneck detection

Requirements:
    pip install torch torchvision
"""

import tracesmith as ts
import time
from contextlib import contextmanager
from typing import List, Dict, Tuple, Optional, Callable
from dataclasses import dataclass, field
from collections import defaultdict

try:
    import torch
    import torch.nn as nn
    TORCH_AVAILABLE = True
except ImportError:
    TORCH_AVAILABLE = False
    print("PyTorch not available. Install with: pip install torch torchvision")


@dataclass
class LayerProfile:
    """Profile information for a single layer."""
    name: str
    module_type: str
    forward_time_ms: float = 0
    backward_time_ms: float = 0
    input_shapes: List[Tuple] = field(default_factory=list)
    output_shapes: List[Tuple] = field(default_factory=list)
    param_count: int = 0
    memory_allocated_mb: float = 0
    gpu_events: List[ts.TraceEvent] = field(default_factory=list)

    @property
    def total_time_ms(self) -> float:
        return self.forward_time_ms + self.backward_time_ms


@dataclass
class ModelProfile:
    """Complete model profile."""
    model_name: str
    layer_profiles: Dict[str, LayerProfile] = field(default_factory=dict)
    total_forward_ms: float = 0
    total_backward_ms: float = 0
    gpu_events: List[ts.TraceEvent] = field(default_factory=list)
    timeline: Optional[ts.Timeline] = None


class PyTorchLayerProfiler:
    """
    Profiles PyTorch models at the layer level using hooks.
    
    Combines PyTorch's hook mechanism with TraceSmith GPU profiling
    to provide detailed layer-by-layer performance analysis.
    """

    def __init__(self):
        self.layer_profiles: Dict[str, LayerProfile] = {}
        self.hooks: List = []
        self.layer_stack: List[str] = []
        self.profiler: Optional[ts.IPlatformProfiler] = None
        self.is_profiling = False
        self._init_gpu_profiler()

    def _init_gpu_profiler(self):
        """Initialize GPU profiler."""
        platform = ts.detect_platform()
        if platform != ts.PlatformType.Unknown:
            self.profiler = ts.create_profiler(platform)
            config = ts.ProfilerConfig()
            config.buffer_size = 500000
            config.capture_kernels = True
            config.capture_memcpy = True
            config.capture_alloc = True
            self.profiler.initialize(config)

    def register_hooks(self, model: nn.Module, prefix: str = "", forward_only: bool = False):
        """Register forward and backward hooks on all layers.
        
        Args:
            model: PyTorch model to profile
            prefix: Prefix for layer names
            forward_only: If True, only register forward hooks (safer for training)
        """
        for name, module in model.named_modules():
            if len(list(module.children())) == 0:  # Leaf modules only
                full_name = f"{prefix}{name}" if name else prefix
                if not full_name:
                    continue

                # Initialize layer profile
                param_count = sum(p.numel() for p in module.parameters())
                self.layer_profiles[full_name] = LayerProfile(
                    name=full_name,
                    module_type=module.__class__.__name__,
                    param_count=param_count
                )

                # Register hooks
                forward_hook = self._create_forward_hook(full_name)
                self.hooks.append(module.register_forward_hook(forward_hook))
                
                # Backward hooks can interfere with autograd in training
                if not forward_only:
                    backward_hook = self._create_backward_hook(full_name)
                    self.hooks.append(module.register_full_backward_hook(backward_hook))

    def _create_forward_hook(self, layer_name: str) -> Callable:
        """Create forward hook for a layer."""
        def hook(module, input, output):
            profile = self.layer_profiles[layer_name]

            # Record input/output shapes
            def get_shapes(x):
                if isinstance(x, torch.Tensor):
                    return [tuple(x.shape)]
                elif isinstance(x, (tuple, list)):
                    shapes = []
                    for item in x:
                        shapes.extend(get_shapes(item))
                    return shapes
                return []

            profile.input_shapes = get_shapes(input)
            profile.output_shapes = get_shapes(output)

            # Record memory
            if torch.cuda.is_available():
                profile.memory_allocated_mb = torch.cuda.memory_allocated() / 1e6

        return hook

    def _create_backward_hook(self, layer_name: str) -> Callable:
        """Create backward hook for a layer."""
        def hook(module, grad_input, grad_output):
            pass  # Timing handled by markers
        return hook

    def remove_hooks(self):
        """Remove all registered hooks."""
        for hook in self.hooks:
            hook.remove()
        self.hooks.clear()

    @contextmanager
    def profile(self, model: nn.Module):
        """Context manager for profiling a model."""
        self.register_hooks(model)

        # Start GPU profiling
        if self.profiler:
            self.profiler.start_capture()

        if torch.cuda.is_available():
            torch.cuda.synchronize()

        start_time = time.perf_counter()
        self.is_profiling = True

        try:
            yield self
        finally:
            if torch.cuda.is_available():
                torch.cuda.synchronize()

            end_time = time.perf_counter()
            self.is_profiling = False

            # Stop GPU profiling
            if self.profiler:
                self.profiler.stop_capture()
                self.gpu_events = self.profiler.get_events()
            else:
                self.gpu_events = []

            self.total_time_ms = (end_time - start_time) * 1000
            self.remove_hooks()

    def profile_forward_pass(self, model: nn.Module, input_data: torch.Tensor,
                            num_iterations: int = 10, warmup: int = 3) -> ModelProfile:
        """Profile forward pass only."""
        device = next(model.parameters()).device
        input_data = input_data.to(device)

        # Warmup
        model.eval()
        with torch.no_grad():
            for _ in range(warmup):
                _ = model(input_data)
                if torch.cuda.is_available():
                    torch.cuda.synchronize()

        # Profile
        self.register_hooks(model)

        if self.profiler:
            self.profiler.start_capture()

        if torch.cuda.is_available():
            torch.cuda.synchronize()

        start_time = time.perf_counter()

        with torch.no_grad():
            for i in range(num_iterations):
                _ = model(input_data)

        if torch.cuda.is_available():
            torch.cuda.synchronize()

        end_time = time.perf_counter()

        if self.profiler:
            self.profiler.stop_capture()
            gpu_events = self.profiler.get_events()
        else:
            gpu_events = []

        self.remove_hooks()

        # Build result
        result = ModelProfile(
            model_name=model.__class__.__name__,
            layer_profiles=self.layer_profiles.copy(),
            total_forward_ms=(end_time - start_time) * 1000 / num_iterations,
            gpu_events=gpu_events,
            timeline=ts.build_timeline(gpu_events) if gpu_events else None
        )

        return result

    def profile_training_step(self, model: nn.Module, input_data: torch.Tensor,
                             target: torch.Tensor, optimizer: torch.optim.Optimizer,
                             criterion: nn.Module = None) -> ModelProfile:
        """Profile a complete training step (forward + backward + optimizer)."""
        device = next(model.parameters()).device
        input_data = input_data.to(device)
        target = target.to(device)

        if criterion is None:
            criterion = nn.CrossEntropyLoss()

        # Warmup
        model.train()
        for _ in range(3):
            optimizer.zero_grad()
            output = model(input_data)
            loss = criterion(output, target)
            loss.backward()
            optimizer.step()
            if torch.cuda.is_available():
                torch.cuda.synchronize()

        # Profile - use forward_only=True to avoid autograd issues with backward hooks
        self.register_hooks(model, forward_only=True)

        if self.profiler:
            self.profiler.start_capture()

        if torch.cuda.is_available():
            torch.cuda.synchronize()

        # Forward timing
        forward_start = time.perf_counter()
        optimizer.zero_grad()
        output = model(input_data)
        loss = criterion(output, target)

        if torch.cuda.is_available():
            torch.cuda.synchronize()
        forward_end = time.perf_counter()

        # Backward timing
        backward_start = time.perf_counter()
        loss.backward()

        if torch.cuda.is_available():
            torch.cuda.synchronize()
        backward_end = time.perf_counter()

        # Optimizer timing (part of backward in profiling context)
        optimizer.step()

        if torch.cuda.is_available():
            torch.cuda.synchronize()

        if self.profiler:
            self.profiler.stop_capture()
            gpu_events = self.profiler.get_events()
        else:
            gpu_events = []

        self.remove_hooks()

        result = ModelProfile(
            model_name=model.__class__.__name__,
            layer_profiles=self.layer_profiles.copy(),
            total_forward_ms=(forward_end - forward_start) * 1000,
            total_backward_ms=(backward_end - backward_start) * 1000,
            gpu_events=gpu_events,
            timeline=ts.build_timeline(gpu_events) if gpu_events else None
        )

        return result


def print_layer_profile(profile: ModelProfile):
    """Print detailed layer-by-layer profiling results."""
    print("\n" + "=" * 90)
    print(f"MODEL PROFILE: {profile.model_name}")
    print("=" * 90)
    print(f"Forward Time:  {profile.total_forward_ms:.3f} ms")
    print(f"Backward Time: {profile.total_backward_ms:.3f} ms")
    print(f"Total Events:  {len(profile.gpu_events)}")

    if profile.timeline:
        print(f"GPU Duration:  {profile.timeline.total_duration / 1e6:.3f} ms")
        print(f"GPU Util:      {profile.timeline.gpu_utilization * 100:.1f}%")

    print("\n" + "-" * 90)
    print("LAYER BREAKDOWN:")
    print("-" * 90)
    print(f"{'Layer Name':<40} {'Type':<15} {'Params':>10} {'Memory(MB)':>12}")
    print("-" * 90)

    # Sort by parameter count
    sorted_layers = sorted(
        profile.layer_profiles.values(),
        key=lambda x: x.param_count,
        reverse=True
    )

    for layer in sorted_layers[:20]:
        name = layer.name[:38] + ".." if len(layer.name) > 40 else layer.name
        print(f"{name:<40} {layer.module_type:<15} {layer.param_count:>10,} "
              f"{layer.memory_allocated_mb:>12.2f}")

    if len(sorted_layers) > 20:
        print(f"... and {len(sorted_layers) - 20} more layers")

    # Kernel statistics
    if profile.gpu_events:
        print("\n" + "-" * 90)
        print("GPU KERNEL STATISTICS:")
        print("-" * 90)

        kernel_times: Dict[str, Tuple[int, int]] = {}  # name -> (count, total_ns)
        for event in profile.gpu_events:
            if event.type == ts.EventType.KernelLaunch:
                name = event.name
                count, total = kernel_times.get(name, (0, 0))
                kernel_times[name] = (count + 1, total + event.duration)

        print(f"{'Kernel Name':<50} {'Count':>8} {'Total(ms)':>12}")
        print("-" * 90)

        sorted_kernels = sorted(kernel_times.items(), key=lambda x: x[1][1], reverse=True)
        for name, (count, total_ns) in sorted_kernels[:15]:
            display_name = name[:48] + ".." if len(name) > 50 else name
            print(f"{display_name:<50} {count:>8} {total_ns / 1e6:>12.3f}")

    print("=" * 90)


def analyze_bottlenecks(profile: ModelProfile) -> List[str]:
    """Identify potential performance bottlenecks."""
    bottlenecks = []

    # Analyze kernel distribution
    kernel_times: Dict[str, int] = {}
    for event in profile.gpu_events:
        if event.type == ts.EventType.KernelLaunch:
            name = event.name
            kernel_times[name] = kernel_times.get(name, 0) + event.duration

    total_kernel_time = sum(kernel_times.values())

    if total_kernel_time > 0:
        # Find kernels that dominate execution time
        for name, time_ns in kernel_times.items():
            pct = time_ns / total_kernel_time * 100
            if pct > 20:
                bottlenecks.append(
                    f"Kernel '{name}' takes {pct:.1f}% of total kernel time"
                )

    # Check for memory transfer overhead
    memcpy_time = 0
    for event in profile.gpu_events:
        if event.type in [ts.EventType.MemcpyH2D, ts.EventType.MemcpyD2H]:
            memcpy_time += event.duration

    if total_kernel_time > 0 and memcpy_time > total_kernel_time * 0.1:
        bottlenecks.append(
            f"Memory transfers take {memcpy_time / total_kernel_time * 100:.1f}% "
            "of kernel time (consider pinned memory)"
        )

    # Check GPU utilization
    if profile.timeline and profile.timeline.gpu_utilization < 0.5:
        bottlenecks.append(
            f"Low GPU utilization ({profile.timeline.gpu_utilization * 100:.1f}%) "
            "- consider larger batch sizes or async operations"
        )

    return bottlenecks


# Example models for testing

class ResNetBlock(nn.Module):
    """ResNet-style residual block."""

    def __init__(self, in_channels, out_channels, stride=1):
        super().__init__()
        self.conv1 = nn.Conv2d(in_channels, out_channels, 3, stride, 1, bias=False)
        self.bn1 = nn.BatchNorm2d(out_channels)
        self.conv2 = nn.Conv2d(out_channels, out_channels, 3, 1, 1, bias=False)
        self.bn2 = nn.BatchNorm2d(out_channels)

        self.shortcut = nn.Sequential()
        if stride != 1 or in_channels != out_channels:
            self.shortcut = nn.Sequential(
                nn.Conv2d(in_channels, out_channels, 1, stride, bias=False),
                nn.BatchNorm2d(out_channels)
            )

    def forward(self, x):
        out = torch.relu(self.bn1(self.conv1(x)))
        out = self.bn2(self.conv2(out))
        out += self.shortcut(x)
        out = torch.relu(out)
        return out


class MiniResNet(nn.Module):
    """Minimal ResNet for demonstration."""

    def __init__(self, num_classes=10):
        super().__init__()
        self.conv1 = nn.Conv2d(3, 64, 3, 1, 1, bias=False)
        self.bn1 = nn.BatchNorm2d(64)

        self.layer1 = self._make_layer(64, 64, 2)
        self.layer2 = self._make_layer(64, 128, 2, stride=2)
        self.layer3 = self._make_layer(128, 256, 2, stride=2)

        self.avgpool = nn.AdaptiveAvgPool2d(1)
        self.fc = nn.Linear(256, num_classes)

    def _make_layer(self, in_ch, out_ch, num_blocks, stride=1):
        layers = [ResNetBlock(in_ch, out_ch, stride)]
        for _ in range(1, num_blocks):
            layers.append(ResNetBlock(out_ch, out_ch))
        return nn.Sequential(*layers)

    def forward(self, x):
        x = torch.relu(self.bn1(self.conv1(x)))
        x = self.layer1(x)
        x = self.layer2(x)
        x = self.layer3(x)
        x = self.avgpool(x)
        x = x.view(x.size(0), -1)
        x = self.fc(x)
        return x


def main():
    print("╔═══════════════════════════════════════════════════════════╗")
    print("║  TraceSmith - PyTorch Hooks-Based Layer Profiling         ║")
    print("╚═══════════════════════════════════════════════════════════╝\n")

    if not TORCH_AVAILABLE:
        print("PyTorch is required for this example.")
        return

    platform = ts.detect_platform()
    print(f"Platform: {ts.platform_type_to_string(platform)}")

    if torch.cuda.is_available():
        print(f"CUDA: {torch.cuda.get_device_name(0)}")
        device = torch.device("cuda")
    else:
        print("CUDA not available, using CPU")
        device = torch.device("cpu")

    # Create model
    print("\nCreating MiniResNet model...")
    model = MiniResNet(num_classes=100).to(device)

    total_params = sum(p.numel() for p in model.parameters())
    print(f"Total parameters: {total_params:,}")

    # Create sample data
    batch_size = 64
    input_images = torch.randn(batch_size, 3, 32, 32)
    target = torch.randint(0, 100, (batch_size,))

    # Profile forward pass
    print("\n" + "-" * 60)
    print("Profiling Forward Pass...")
    print("-" * 60)

    profiler = PyTorchLayerProfiler()
    forward_profile = profiler.profile_forward_pass(
        model, input_images, num_iterations=20, warmup=5
    )
    print_layer_profile(forward_profile)

    # Profile training step
    print("\n" + "-" * 60)
    print("Profiling Training Step...")
    print("-" * 60)

    optimizer = torch.optim.SGD(model.parameters(), lr=0.01, momentum=0.9)
    profiler = PyTorchLayerProfiler()
    train_profile = profiler.profile_training_step(
        model, input_images, target, optimizer
    )
    print_layer_profile(train_profile)

    # Analyze bottlenecks
    print("\n" + "-" * 60)
    print("BOTTLENECK ANALYSIS:")
    print("-" * 60)

    bottlenecks = analyze_bottlenecks(train_profile)
    if bottlenecks:
        for i, bottleneck in enumerate(bottlenecks, 1):
            print(f"  {i}. {bottleneck}")
    else:
        print("  No significant bottlenecks detected.")

    # Export traces
    if forward_profile.gpu_events:
        ts.export_perfetto(forward_profile.gpu_events, "resnet_forward_trace.json")
        print("\n✓ Exported: resnet_forward_trace.json")

    if train_profile.gpu_events:
        ts.export_perfetto(train_profile.gpu_events, "resnet_training_trace.json")
        print("✓ Exported: resnet_training_trace.json")

    print("\nView traces at: https://ui.perfetto.dev/")


if __name__ == "__main__":
    main()
