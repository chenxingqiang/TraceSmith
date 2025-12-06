#!/usr/bin/env python3
"""
TraceSmith Examples Test Runner

Runs all example scripts across available devices (CUDA, MPS, CPU).
Provides a summary of which examples pass/fail on each device.

Usage:
    python run_tests.py                    # Run all tests on best device
    python run_tests.py --device cuda      # Run on specific device
    python run_tests.py --all-devices      # Run on all available devices
    python run_tests.py --list             # List available tests
    python run_tests.py --test basic       # Run specific test
"""

import argparse
import subprocess
import sys
import os
import time
from pathlib import Path
from typing import List, Dict, Optional, Tuple
from dataclasses import dataclass
from enum import Enum

# Add examples directory to path
EXAMPLES_DIR = Path(__file__).parent
sys.path.insert(0, str(EXAMPLES_DIR))

try:
    from device_utils import DeviceManager, DeviceType
    DEVICE_UTILS_AVAILABLE = True
except ImportError:
    DEVICE_UTILS_AVAILABLE = False
    DeviceType = None


class TestStatus(Enum):
    PASSED = "✓ PASSED"
    FAILED = "✗ FAILED"
    SKIPPED = "○ SKIPPED"
    ERROR = "⚠ ERROR"


@dataclass
class TestResult:
    """Result of a single test run."""
    name: str
    device: str
    status: TestStatus
    duration_s: float
    output: str
    error: Optional[str] = None


# Define available tests
TESTS = {
    "basic": {
        "file": "basic_usage.py",
        "description": "Basic TraceSmith usage with real GPU profiling",
        "requires_gpu": False,
    },
    "pytorch": {
        "file": "pytorch_profiling.py",
        "description": "PyTorch model profiling",
        "requires_gpu": True,
        "requires": ["torch"],
    },
    "hooks": {
        "file": "pytorch_hooks_profiling.py",
        "description": "PyTorch hooks for layer profiling",
        "requires_gpu": True,
        "requires": ["torch"],
    },
    "memory": {
        "file": "memory_profiling.py",
        "description": "GPU memory profiling",
        "requires_gpu": True,
        "requires": ["torch"],
    },
    "kernel": {
        "file": "kernel_timing_stats.py",
        "description": "Kernel timing statistics",
        "requires_gpu": True,
        "requires": ["torch"],
    },
    "realtime": {
        "file": "realtime_tracing.py",
        "description": "Real-time tracing",
        "requires_gpu": False,
    },
    "multigpu": {
        "file": "multi_gpu_profiling.py",
        "description": "Multi-GPU profiling",
        "requires_gpu": True,
        "requires": ["torch"],
    },
    "transformers": {
        "file": "transformers_profiling.py",
        "description": "Transformer/LLM profiling",
        "requires_gpu": True,
        "requires": ["torch"],
    },
}


def check_requirements(test_info: dict) -> Tuple[bool, str]:
    """Check if test requirements are met."""
    requires = test_info.get("requires", [])
    
    for req in requires:
        try:
            __import__(req)
        except ImportError:
            return False, f"Missing requirement: {req}"
    
    return True, ""


def get_available_devices() -> List[str]:
    """Get list of available devices."""
    devices = ["cpu"]
    
    if DEVICE_UTILS_AVAILABLE:
        dm = DeviceManager()
        for dev in dm.list_devices():
            if dev.device_type != DeviceType.CPU:
                devices.insert(0, dev.device_type.value)
    else:
        # Fallback detection
        try:
            import torch
            if torch.cuda.is_available():
                devices.insert(0, "cuda")
            if hasattr(torch.backends, 'mps') and torch.backends.mps.is_available():
                devices.insert(0, "mps")
        except ImportError:
            pass
    
    return list(dict.fromkeys(devices))  # Remove duplicates, preserve order


def run_test(test_name: str, test_info: dict, device: str, 
             timeout: int = 120, verbose: bool = False) -> TestResult:
    """Run a single test."""
    start_time = time.time()
    
    # Check requirements
    req_ok, req_msg = check_requirements(test_info)
    if not req_ok:
        return TestResult(
            name=test_name,
            device=device,
            status=TestStatus.SKIPPED,
            duration_s=0,
            output="",
            error=req_msg
        )
    
    # Check GPU requirement
    if test_info.get("requires_gpu") and device == "cpu":
        # Still try to run, but might skip GPU-specific parts
        pass
    
    # Build command
    script_path = EXAMPLES_DIR / test_info["file"]
    if not script_path.exists():
        return TestResult(
            name=test_name,
            device=device,
            status=TestStatus.ERROR,
            duration_s=0,
            output="",
            error=f"Script not found: {script_path}"
        )
    
    cmd = [sys.executable, str(script_path)]
    
    # Add device argument if supported
    if device != "cpu":
        cmd.extend(["--device", device])
    
    # Run the test
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=str(EXAMPLES_DIR),
            env={**os.environ, "PYTHONPATH": str(EXAMPLES_DIR)}
        )
        
        duration = time.time() - start_time
        
        if result.returncode == 0:
            status = TestStatus.PASSED
        else:
            status = TestStatus.FAILED
        
        output = result.stdout
        error = result.stderr if result.returncode != 0 else None
        
        if verbose:
            print(result.stdout)
            if result.stderr:
                print(result.stderr, file=sys.stderr)
        
        return TestResult(
            name=test_name,
            device=device,
            status=status,
            duration_s=duration,
            output=output,
            error=error
        )
        
    except subprocess.TimeoutExpired:
        return TestResult(
            name=test_name,
            device=device,
            status=TestStatus.ERROR,
            duration_s=timeout,
            output="",
            error=f"Timeout after {timeout}s"
        )
    except Exception as e:
        return TestResult(
            name=test_name,
            device=device,
            status=TestStatus.ERROR,
            duration_s=time.time() - start_time,
            output="",
            error=str(e)
        )


def print_results(results: List[TestResult]):
    """Print test results summary."""
    print("\n" + "=" * 70)
    print("TEST RESULTS SUMMARY")
    print("=" * 70)
    
    # Group by device
    by_device: Dict[str, List[TestResult]] = {}
    for r in results:
        if r.device not in by_device:
            by_device[r.device] = []
        by_device[r.device].append(r)
    
    for device, device_results in by_device.items():
        print(f"\n{device.upper()}:")
        print("-" * 50)
        
        passed = sum(1 for r in device_results if r.status == TestStatus.PASSED)
        failed = sum(1 for r in device_results if r.status == TestStatus.FAILED)
        skipped = sum(1 for r in device_results if r.status == TestStatus.SKIPPED)
        errors = sum(1 for r in device_results if r.status == TestStatus.ERROR)
        
        for r in device_results:
            status_str = r.status.value
            duration_str = f"({r.duration_s:.1f}s)" if r.duration_s > 0 else ""
            error_str = f" - {r.error}" if r.error else ""
            print(f"  {status_str} {r.name:20} {duration_str:10} {error_str}")
        
        print(f"\n  Total: {passed} passed, {failed} failed, {skipped} skipped, {errors} errors")
    
    # Overall summary
    total_passed = sum(1 for r in results if r.status == TestStatus.PASSED)
    total_failed = sum(1 for r in results if r.status == TestStatus.FAILED)
    total_tests = len(results)
    
    print("\n" + "=" * 70)
    print(f"OVERALL: {total_passed}/{total_tests} tests passed")
    
    if total_failed > 0:
        print("\nFailed tests:")
        for r in results:
            if r.status == TestStatus.FAILED:
                print(f"  - {r.name} on {r.device}")
                if r.error:
                    # Print first few lines of error
                    error_lines = r.error.strip().split('\n')[:5]
                    for line in error_lines:
                        print(f"      {line}")


def list_tests():
    """List all available tests."""
    print("Available Tests:")
    print("=" * 60)
    
    for name, info in TESTS.items():
        req_ok, _ = check_requirements(info)
        status = "✓" if req_ok else "○ (missing deps)"
        gpu_req = "GPU" if info.get("requires_gpu") else "CPU"
        print(f"  {name:15} [{gpu_req:3}] {status} - {info['description']}")
    
    print("\nAvailable Devices:")
    for dev in get_available_devices():
        print(f"  - {dev}")


def main():
    parser = argparse.ArgumentParser(
        description="TraceSmith Examples Test Runner",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python run_tests.py                    # Run all tests on best device
  python run_tests.py --device cuda      # Run on CUDA
  python run_tests.py --device mps       # Run on Apple Metal
  python run_tests.py --all-devices      # Run on all available devices
  python run_tests.py --test basic       # Run specific test
  python run_tests.py --list             # List available tests
"""
    )
    
    parser.add_argument(
        "--device", "-d",
        choices=["cuda", "mps", "rocm", "cpu", "auto"],
        default="auto",
        help="Device to run tests on (default: auto)"
    )
    parser.add_argument(
        "--all-devices", "-a",
        action="store_true",
        help="Run tests on all available devices"
    )
    parser.add_argument(
        "--test", "-t",
        choices=list(TESTS.keys()),
        help="Run specific test only"
    )
    parser.add_argument(
        "--list", "-l",
        action="store_true",
        help="List available tests and exit"
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=120,
        help="Timeout per test in seconds (default: 120)"
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Show test output"
    )
    
    args = parser.parse_args()
    
    # List tests
    if args.list:
        list_tests()
        return 0
    
    # Determine devices to test
    if args.all_devices:
        devices = get_available_devices()
    elif args.device == "auto":
        devices = [get_available_devices()[0]]
    else:
        devices = [args.device]
    
    # Determine tests to run
    if args.test:
        tests_to_run = {args.test: TESTS[args.test]}
    else:
        tests_to_run = TESTS
    
    print("TraceSmith Examples Test Runner")
    print("=" * 60)
    print(f"Devices: {', '.join(devices)}")
    print(f"Tests: {', '.join(tests_to_run.keys())}")
    print(f"Timeout: {args.timeout}s per test")
    print("=" * 60)
    
    # Run tests
    results: List[TestResult] = []
    
    for device in devices:
        print(f"\n>>> Running tests on {device.upper()} <<<\n")
        
        for test_name, test_info in tests_to_run.items():
            print(f"Running {test_name}...", end=" ", flush=True)
            
            result = run_test(
                test_name, test_info, device,
                timeout=args.timeout,
                verbose=args.verbose
            )
            results.append(result)
            
            print(f"{result.status.value} ({result.duration_s:.1f}s)")
    
    # Print summary
    print_results(results)
    
    # Return exit code
    failed = sum(1 for r in results if r.status in (TestStatus.FAILED, TestStatus.ERROR))
    return 1 if failed > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
