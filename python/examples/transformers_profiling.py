#!/usr/bin/env python3
"""
TraceSmith Example - Transformers/LLM Model Profiling

Demonstrates profiling for transformer-based models:
- BERT/GPT-style model profiling
- Attention mechanism analysis
- Token throughput measurement
- Memory-efficient inference profiling
- HuggingFace integration patterns

Requirements:
    pip install torch transformers (optional)
"""

import tracesmith as ts
import time
from typing import List, Dict, Optional, Tuple
from dataclasses import dataclass, field
from contextlib import contextmanager

try:
    import torch
    import torch.nn as nn
    import torch.nn.functional as F
    TORCH_AVAILABLE = True
except ImportError:
    TORCH_AVAILABLE = False

try:
    from transformers import AutoModel, AutoTokenizer, AutoConfig
    TRANSFORMERS_AVAILABLE = True
except ImportError:
    TRANSFORMERS_AVAILABLE = False


@dataclass
class InferenceMetrics:
    """Metrics for model inference."""
    batch_size: int
    sequence_length: int
    total_tokens: int
    wall_time_ms: float
    gpu_time_ms: float
    tokens_per_second: float
    latency_per_token_ms: float
    memory_peak_mb: float
    kernel_count: int
    attention_time_ms: float = 0
    ffn_time_ms: float = 0
    embedding_time_ms: float = 0


class TransformerProfiler:
    """Profiler specialized for transformer models."""

    def __init__(self, buffer_size: int = 500000):
        self.platform = ts.detect_platform()
        self.profiler = None
        self.events: List[ts.TraceEvent] = []
        self.buffer_size = buffer_size
        self._init_profiler()

    def _init_profiler(self):
        """Initialize GPU profiler."""
        if self.platform != ts.PlatformType.Unknown:
            self.profiler = ts.create_profiler(self.platform)
            config = ts.ProfilerConfig()
            config.buffer_size = self.buffer_size
            config.capture_kernels = True
            config.capture_memcpy = True
            config.capture_alloc = True
            self.profiler.initialize(config)

    @contextmanager
    def profile(self, sync: bool = True):
        """Context manager for profiling."""
        if TORCH_AVAILABLE and torch.cuda.is_available() and sync:
            torch.cuda.synchronize()
            torch.cuda.reset_peak_memory_stats()

        if self.profiler:
            self.profiler.start_capture()

        start_time = time.perf_counter()

        try:
            yield self
        finally:
            if TORCH_AVAILABLE and torch.cuda.is_available() and sync:
                torch.cuda.synchronize()

            self._wall_time = time.perf_counter() - start_time

            if self.profiler:
                self.profiler.stop_capture()
                self.events = self.profiler.get_events()

    def get_inference_metrics(self, batch_size: int, seq_length: int) -> InferenceMetrics:
        """Calculate inference metrics."""
        total_tokens = batch_size * seq_length
        wall_time_ms = self._wall_time * 1000

        # GPU time from events
        gpu_time_ns = sum(e.duration for e in self.events 
                        if e.type == ts.EventType.KernelLaunch)
        gpu_time_ms = gpu_time_ns / 1e6

        # Kernel count
        kernel_count = sum(1 for e in self.events 
                          if e.type == ts.EventType.KernelLaunch)

        # Memory peak
        memory_peak_mb = 0
        if TORCH_AVAILABLE and torch.cuda.is_available():
            memory_peak_mb = torch.cuda.max_memory_allocated() / 1e6

        # Analyze kernel types
        attention_time_ns = 0
        ffn_time_ns = 0
        embedding_time_ns = 0

        for event in self.events:
            if event.type != ts.EventType.KernelLaunch:
                continue

            name = event.name.lower()
            if any(k in name for k in ['attention', 'softmax', 'bmm', 'addmm']):
                attention_time_ns += event.duration
            elif any(k in name for k in ['linear', 'gemm', 'matmul']):
                ffn_time_ns += event.duration
            elif any(k in name for k in ['embedding', 'gather', 'index']):
                embedding_time_ns += event.duration

        return InferenceMetrics(
            batch_size=batch_size,
            sequence_length=seq_length,
            total_tokens=total_tokens,
            wall_time_ms=wall_time_ms,
            gpu_time_ms=gpu_time_ms,
            tokens_per_second=total_tokens / (wall_time_ms / 1000) if wall_time_ms > 0 else 0,
            latency_per_token_ms=wall_time_ms / total_tokens if total_tokens > 0 else 0,
            memory_peak_mb=memory_peak_mb,
            kernel_count=kernel_count,
            attention_time_ms=attention_time_ns / 1e6,
            ffn_time_ms=ffn_time_ns / 1e6,
            embedding_time_ms=embedding_time_ns / 1e6
        )

    def print_metrics(self, metrics: InferenceMetrics):
        """Print formatted metrics."""
        print("\n" + "=" * 70)
        print("TRANSFORMER INFERENCE METRICS")
        print("=" * 70)
        print(f"Batch Size:        {metrics.batch_size}")
        print(f"Sequence Length:   {metrics.sequence_length}")
        print(f"Total Tokens:      {metrics.total_tokens}")
        print("-" * 70)
        print(f"Wall Time:         {metrics.wall_time_ms:.2f} ms")
        print(f"GPU Kernel Time:   {metrics.gpu_time_ms:.2f} ms")
        print(f"Tokens/Second:     {metrics.tokens_per_second:,.0f}")
        print(f"Latency/Token:     {metrics.latency_per_token_ms:.3f} ms")
        print("-" * 70)
        print(f"Memory Peak:       {metrics.memory_peak_mb:.1f} MB")
        print(f"Kernel Count:      {metrics.kernel_count}")
        print("-" * 70)
        print("Time Breakdown:")
        total_analyzed = metrics.attention_time_ms + metrics.ffn_time_ms + metrics.embedding_time_ms
        if total_analyzed > 0:
            print(f"  Attention:       {metrics.attention_time_ms:.2f} ms "
                  f"({metrics.attention_time_ms/metrics.gpu_time_ms*100:.1f}%)")
            print(f"  FFN/Linear:      {metrics.ffn_time_ms:.2f} ms "
                  f"({metrics.ffn_time_ms/metrics.gpu_time_ms*100:.1f}%)")
            print(f"  Embedding:       {metrics.embedding_time_ms:.2f} ms "
                  f"({metrics.embedding_time_ms/metrics.gpu_time_ms*100:.1f}%)")
        print("=" * 70)

    def analyze_attention_kernels(self) -> Dict[str, Tuple[int, float]]:
        """Analyze attention-related kernels."""
        attention_kernels = {}

        for event in self.events:
            if event.type != ts.EventType.KernelLaunch:
                continue

            name = event.name.lower()
            if any(k in name for k in ['attention', 'softmax', 'bmm', 'scaled_dot']):
                if event.name not in attention_kernels:
                    attention_kernels[event.name] = (0, 0)
                count, total = attention_kernels[event.name]
                attention_kernels[event.name] = (count + 1, total + event.duration)

        return attention_kernels

    def export(self, filename: str) -> bool:
        """Export trace to file."""
        return ts.export_perfetto(self.events, filename)

    def finalize(self):
        """Cleanup."""
        if self.profiler:
            self.profiler.finalize()


# Simple Transformer implementation for testing

class MultiHeadAttention(nn.Module):
    """Multi-head attention implementation."""

    def __init__(self, d_model: int, num_heads: int, dropout: float = 0.1):
        super().__init__()
        assert d_model % num_heads == 0

        self.d_model = d_model
        self.num_heads = num_heads
        self.d_k = d_model // num_heads

        self.w_q = nn.Linear(d_model, d_model)
        self.w_k = nn.Linear(d_model, d_model)
        self.w_v = nn.Linear(d_model, d_model)
        self.w_o = nn.Linear(d_model, d_model)
        self.dropout = nn.Dropout(dropout)

    def forward(self, query, key, value, mask=None):
        batch_size = query.size(0)

        # Linear projections
        Q = self.w_q(query).view(batch_size, -1, self.num_heads, self.d_k).transpose(1, 2)
        K = self.w_k(key).view(batch_size, -1, self.num_heads, self.d_k).transpose(1, 2)
        V = self.w_v(value).view(batch_size, -1, self.num_heads, self.d_k).transpose(1, 2)

        # Scaled dot-product attention
        scores = torch.matmul(Q, K.transpose(-2, -1)) / (self.d_k ** 0.5)

        if mask is not None:
            scores = scores.masked_fill(mask == 0, float('-inf'))

        attn = F.softmax(scores, dim=-1)
        attn = self.dropout(attn)

        context = torch.matmul(attn, V)

        # Concatenate heads
        context = context.transpose(1, 2).contiguous().view(batch_size, -1, self.d_model)

        return self.w_o(context)


class FeedForward(nn.Module):
    """Feed-forward network."""

    def __init__(self, d_model: int, d_ff: int, dropout: float = 0.1):
        super().__init__()
        self.linear1 = nn.Linear(d_model, d_ff)
        self.linear2 = nn.Linear(d_ff, d_model)
        self.dropout = nn.Dropout(dropout)

    def forward(self, x):
        return self.linear2(self.dropout(F.gelu(self.linear1(x))))


class TransformerEncoderLayer(nn.Module):
    """Transformer encoder layer."""

    def __init__(self, d_model: int, num_heads: int, d_ff: int, dropout: float = 0.1):
        super().__init__()
        self.self_attn = MultiHeadAttention(d_model, num_heads, dropout)
        self.ff = FeedForward(d_model, d_ff, dropout)
        self.norm1 = nn.LayerNorm(d_model)
        self.norm2 = nn.LayerNorm(d_model)
        self.dropout = nn.Dropout(dropout)

    def forward(self, x, mask=None):
        # Self-attention with residual
        attn_out = self.self_attn(x, x, x, mask)
        x = self.norm1(x + self.dropout(attn_out))

        # Feed-forward with residual
        ff_out = self.ff(x)
        x = self.norm2(x + self.dropout(ff_out))

        return x


class SimpleTransformerEncoder(nn.Module):
    """Simple transformer encoder for demonstration."""

    def __init__(self, vocab_size: int = 30000, d_model: int = 768,
                 num_heads: int = 12, num_layers: int = 12,
                 d_ff: int = 3072, max_seq_len: int = 512,
                 dropout: float = 0.1):
        super().__init__()

        self.d_model = d_model
        self.embedding = nn.Embedding(vocab_size, d_model)
        self.pos_embedding = nn.Embedding(max_seq_len, d_model)

        self.layers = nn.ModuleList([
            TransformerEncoderLayer(d_model, num_heads, d_ff, dropout)
            for _ in range(num_layers)
        ])

        self.norm = nn.LayerNorm(d_model)
        self.dropout = nn.Dropout(dropout)

    def forward(self, input_ids, attention_mask=None):
        batch_size, seq_len = input_ids.size()

        # Embeddings
        positions = torch.arange(seq_len, device=input_ids.device).unsqueeze(0)
        x = self.embedding(input_ids) + self.pos_embedding(positions)
        x = self.dropout(x)

        # Transformer layers
        for layer in self.layers:
            x = layer(x, attention_mask)

        return self.norm(x)


def profile_transformer_inference():
    """Profile transformer inference at different batch sizes and sequence lengths."""
    if not TORCH_AVAILABLE or not torch.cuda.is_available():
        print("PyTorch with CUDA required")
        return

    device = torch.device("cuda")

    print("\n" + "=" * 70)
    print("TRANSFORMER INFERENCE PROFILING")
    print("=" * 70)

    # Create model (BERT-base like configuration)
    model = SimpleTransformerEncoder(
        vocab_size=30000,
        d_model=768,
        num_heads=12,
        num_layers=12,
        d_ff=3072,
        max_seq_len=512
    ).to(device)
    model.eval()

    param_count = sum(p.numel() for p in model.parameters())
    print(f"\nModel: BERT-base like ({param_count / 1e6:.1f}M parameters)")

    # Test configurations
    configs = [
        (1, 32),    # Single, short
        (1, 128),   # Single, medium
        (1, 512),   # Single, long
        (8, 128),   # Batch, medium
        (32, 128),  # Large batch
        (8, 512),   # Batch, long
    ]

    profiler = TransformerProfiler()

    results = []
    for batch_size, seq_len in configs:
        print(f"\nProfiling batch_size={batch_size}, seq_len={seq_len}...")

        # Create input
        input_ids = torch.randint(0, 30000, (batch_size, seq_len), device=device)

        # Warmup
        with torch.no_grad():
            for _ in range(3):
                _ = model(input_ids)
                torch.cuda.synchronize()

        # Profile
        with profiler.profile():
            with torch.no_grad():
                for _ in range(5):  # 5 iterations
                    _ = model(input_ids)

        metrics = profiler.get_inference_metrics(batch_size * 5, seq_len)
        results.append(metrics)

        print(f"  Throughput: {metrics.tokens_per_second:,.0f} tokens/sec")
        print(f"  Latency: {metrics.latency_per_token_ms:.3f} ms/token")
        print(f"  Memory: {metrics.memory_peak_mb:.1f} MB")

    # Summary table
    print("\n" + "=" * 70)
    print("SUMMARY")
    print("=" * 70)
    print(f"{'Batch':>6} {'SeqLen':>8} {'Throughput':>12} {'Latency':>10} {'Memory':>10}")
    print(f"{'Size':>6} {'':>8} {'(tok/s)':>12} {'(ms/tok)':>10} {'(MB)':>10}")
    print("-" * 70)

    for m in results:
        print(f"{m.batch_size:>6} {m.sequence_length:>8} "
              f"{m.tokens_per_second:>12,.0f} {m.latency_per_token_ms:>10.3f} "
              f"{m.memory_peak_mb:>10.1f}")

    # Export last trace
    profiler.export("transformer_inference_trace.json")
    print("\n✓ Exported: transformer_inference_trace.json")

    profiler.finalize()
    return results


def profile_huggingface_model():
    """Profile a HuggingFace transformer model."""
    if not TRANSFORMERS_AVAILABLE:
        print("\nHuggingFace transformers not available")
        print("Install with: pip install transformers")
        return

    if not TORCH_AVAILABLE or not torch.cuda.is_available():
        print("\nPyTorch with CUDA required")
        return

    print("\n" + "=" * 70)
    print("HUGGINGFACE MODEL PROFILING")
    print("=" * 70)

    model_name = "bert-base-uncased"
    print(f"\nLoading {model_name}...")

    try:
        tokenizer = AutoTokenizer.from_pretrained(model_name)
        model = AutoModel.from_pretrained(model_name)
        model = model.cuda()
        model.eval()
    except Exception as e:
        print(f"Could not load model: {e}")
        print("This example requires downloading the model from HuggingFace")
        return

    # Sample text
    texts = [
        "The quick brown fox jumps over the lazy dog.",
        "Machine learning is transforming how we build software applications.",
        "TraceSmith provides comprehensive GPU profiling for deep learning.",
        "Transformer models have revolutionized natural language processing.",
    ]

    # Tokenize
    inputs = tokenizer(texts, padding=True, truncation=True, 
                       max_length=128, return_tensors="pt")
    inputs = {k: v.cuda() for k, v in inputs.items()}

    batch_size = len(texts)
    seq_len = inputs["input_ids"].shape[1]

    print(f"Batch size: {batch_size}")
    print(f"Sequence length: {seq_len}")

    # Profile
    profiler = TransformerProfiler()

    # Warmup
    with torch.no_grad():
        for _ in range(3):
            _ = model(**inputs)
            torch.cuda.synchronize()

    # Profile multiple iterations
    num_iterations = 20
    with profiler.profile():
        with torch.no_grad():
            for _ in range(num_iterations):
                _ = model(**inputs)

    metrics = profiler.get_inference_metrics(batch_size * num_iterations, seq_len)
    profiler.print_metrics(metrics)

    # Analyze attention kernels
    print("\nAttention Kernel Analysis:")
    print("-" * 50)
    attention_kernels = profiler.analyze_attention_kernels()
    sorted_kernels = sorted(attention_kernels.items(), 
                           key=lambda x: x[1][1], reverse=True)
    for name, (count, total_ns) in sorted_kernels[:10]:
        avg_us = (total_ns / count) / 1000 if count > 0 else 0
        print(f"  {name[:45]:<45} x{count:>4} avg={avg_us:.1f}µs")

    profiler.export("huggingface_trace.json")
    print("\n✓ Exported: huggingface_trace.json")

    profiler.finalize()


def benchmark_attention_implementations():
    """Compare different attention implementations."""
    if not TORCH_AVAILABLE or not torch.cuda.is_available():
        print("PyTorch with CUDA required")
        return

    print("\n" + "=" * 70)
    print("ATTENTION IMPLEMENTATION BENCHMARK")
    print("=" * 70)

    device = torch.device("cuda")
    batch_size = 8
    seq_len = 512
    d_model = 768
    num_heads = 12

    # Standard attention
    print("\n1. Standard Multi-Head Attention")
    attn_standard = MultiHeadAttention(d_model, num_heads).to(device).eval()

    x = torch.randn(batch_size, seq_len, d_model, device=device)

    profiler = TransformerProfiler()

    # Warmup
    with torch.no_grad():
        for _ in range(5):
            _ = attn_standard(x, x, x)
            torch.cuda.synchronize()

    # Profile
    num_iterations = 20
    with profiler.profile():
        with torch.no_grad():
            for _ in range(num_iterations):
                _ = attn_standard(x, x, x)

    timeline = ts.build_timeline(profiler.events)
    print(f"   GPU time: {timeline.total_duration / 1e6 / num_iterations:.3f} ms per iteration")

    # PyTorch native attention (if available)
    if hasattr(F, 'scaled_dot_product_attention'):
        print("\n2. PyTorch Flash Attention (scaled_dot_product_attention)")

        Q = torch.randn(batch_size, num_heads, seq_len, d_model // num_heads, device=device)
        K = torch.randn(batch_size, num_heads, seq_len, d_model // num_heads, device=device)
        V = torch.randn(batch_size, num_heads, seq_len, d_model // num_heads, device=device)

        # Warmup
        with torch.no_grad():
            for _ in range(5):
                _ = F.scaled_dot_product_attention(Q, K, V)
                torch.cuda.synchronize()

        profiler2 = TransformerProfiler()
        with profiler2.profile():
            with torch.no_grad():
                for _ in range(num_iterations):
                    _ = F.scaled_dot_product_attention(Q, K, V)

        timeline2 = ts.build_timeline(profiler2.events)
        print(f"   GPU time: {timeline2.total_duration / 1e6 / num_iterations:.3f} ms per iteration")

        speedup = (timeline.total_duration / timeline2.total_duration) if timeline2.total_duration > 0 else 0
        print(f"   Speedup: {speedup:.2f}x")

    print("\n" + "=" * 70)


def main():
    print("╔═══════════════════════════════════════════════════════════╗")
    print("║  TraceSmith - Transformer Model Profiling                 ║")
    print("╚═══════════════════════════════════════════════════════════╝\n")

    print(f"TraceSmith Version: {ts.__version__}")
    print(f"Platform: {ts.platform_type_to_string(ts.detect_platform())}")

    if TORCH_AVAILABLE:
        print(f"PyTorch: {torch.__version__}")
        if torch.cuda.is_available():
            print(f"CUDA: {torch.cuda.get_device_name(0)}")
    else:
        print("PyTorch not available")

    if TRANSFORMERS_AVAILABLE:
        import transformers
        print(f"Transformers: {transformers.__version__}")
    else:
        print("HuggingFace Transformers not available")

    # Run profiling examples
    profile_transformer_inference()
    profile_huggingface_model()
    benchmark_attention_implementations()

    print("\n" + "=" * 60)
    print("Transformer Profiling Complete!")
    print("=" * 60)
    print("\nGenerated trace files:")
    print("  - transformer_inference_trace.json")
    print("  - huggingface_trace.json (if HuggingFace available)")
    print("\nView at: https://ui.perfetto.dev/")


if __name__ == "__main__":
    main()
