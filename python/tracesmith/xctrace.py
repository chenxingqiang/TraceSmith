"""
TraceSmith - Apple Instruments (xctrace) Integration

Provides integration with Apple's Instruments/xctrace for Metal GPU profiling
on macOS. This module wraps xctrace commands and parses the output into
TraceSmith events.

Usage:
    from tracesmith.xctrace import XCTraceProfiler
    
    profiler = XCTraceProfiler()
    events = profiler.profile_command(["python", "train.py"], duration=10)
    
    # Or use the context manager
    with profiler.profile():
        # Your code here
        pass
    events = profiler.get_events()
"""

import os
import sys
import subprocess
import tempfile
import shutil
import xml.etree.ElementTree as ET
from typing import List, Optional, Dict, Any, Tuple
from dataclasses import dataclass, field
from datetime import datetime
import re
import threading
import time

# Import TraceSmith types
try:
    from . import (
        TraceEvent, EventType, PlatformType,
        get_current_timestamp, export_perfetto
    )
except ImportError:
    # Fallback for standalone testing
    TraceEvent = None
    EventType = None


@dataclass
class XCTraceConfig:
    """Configuration for xctrace profiling."""
    template: str = "Metal System Trace"
    duration_seconds: int = 10
    output_dir: str = "./traces"
    export_format: str = "xml"  # xml or har
    include_system_processes: bool = False
    
    # Available templates for Metal/GPU profiling
    METAL_TEMPLATES = [
        "Metal System Trace",  # Most detailed Metal profiling
        "GPU Driver",          # Driver-level analysis
        "Game Performance",    # Frame rate and GPU time
        "Animation Hitches",   # Animation performance
    ]


@dataclass 
class MetalGPUEvent:
    """Parsed Metal GPU event from xctrace."""
    timestamp_ns: int
    channel_id: str
    function: int
    slot_id: int
    gpu_submission_id: str
    accelerator_id: int
    note: str = ""
    
    def to_trace_event(self) -> 'TraceEvent':
        """Convert to TraceSmith TraceEvent."""
        if TraceEvent is None:
            raise ImportError("TraceSmith not available")
        
        event = TraceEvent()
        event.timestamp = self.timestamp_ns
        event.type = EventType.KernelLaunch if self.function == 1 else EventType.KernelComplete
        event.name = f"metal_cmd_{self.channel_id}"
        event.correlation_id = int(self.gpu_submission_id, 16) if self.gpu_submission_id.startswith("0x") else int(self.gpu_submission_id)
        event.device_id = 0
        event.stream_id = int(self.channel_id, 16) if self.channel_id.startswith("0x") else 0
        return event


@dataclass
class MetalDriverInterval:
    """Parsed Metal driver interval from xctrace."""
    start_ns: int
    duration_ns: int
    name: str
    thread_id: int = 0
    
    def to_trace_event(self) -> 'TraceEvent':
        """Convert to TraceSmith TraceEvent."""
        if TraceEvent is None:
            raise ImportError("TraceSmith not available")
        
        event = TraceEvent()
        event.timestamp = self.start_ns
        event.duration = self.duration_ns
        event.type = EventType.KernelLaunch
        event.name = self.name
        event.thread_id = self.thread_id
        event.device_id = 0
        return event


class XCTraceProfiler:
    """
    Profiler that uses Apple Instruments (xctrace) for Metal GPU analysis.
    
    This provides real GPU event capture on macOS by leveraging Apple's
    official profiling tools.
    """
    
    def __init__(self, config: Optional[XCTraceConfig] = None):
        self.config = config or XCTraceConfig()
        self._trace_file: Optional[str] = None
        self._events: List[TraceEvent] = []
        self._process: Optional[subprocess.Popen] = None
        self._temp_dir: Optional[str] = None
        
        # Check if xctrace is available
        if not self.is_available():
            raise RuntimeError(
                "xctrace not found. Please install Xcode Command Line Tools:\n"
                "  xcode-select --install"
            )
    
    @staticmethod
    def is_available() -> bool:
        """Check if xctrace is available on this system."""
        if sys.platform != "darwin":
            return False
        try:
            result = subprocess.run(
                ["xcrun", "xctrace", "version"],
                capture_output=True,
                text=True,
                timeout=5
            )
            return result.returncode == 0
        except (subprocess.SubprocessError, FileNotFoundError):
            return False
    
    @staticmethod
    def list_templates() -> List[str]:
        """List available Instruments templates."""
        try:
            result = subprocess.run(
                ["xcrun", "xctrace", "list", "templates"],
                capture_output=True,
                text=True,
                timeout=10
            )
            if result.returncode == 0:
                return result.stdout.strip().split("\n")
        except subprocess.SubprocessError:
            pass
        return []
    
    @staticmethod
    def get_metal_templates() -> List[str]:
        """Get templates relevant for Metal/GPU profiling."""
        all_templates = XCTraceProfiler.list_templates()
        metal_keywords = ["Metal", "GPU", "Game", "Animation", "Graphics"]
        return [t for t in all_templates 
                if any(kw.lower() in t.lower() for kw in metal_keywords)]
    
    def profile_command(
        self,
        command: List[str],
        duration: Optional[int] = None,
        output_file: Optional[str] = None
    ) -> List['TraceEvent']:
        """
        Profile a command using xctrace.
        
        Args:
            command: Command and arguments to profile
            duration: Maximum duration in seconds (None = until command exits)
            output_file: Optional output trace file path
            
        Returns:
            List of TraceEvent objects
        """
        if not command:
            raise ValueError("Command cannot be empty")
        
        # Resolve command path
        cmd_path = shutil.which(command[0])
        if cmd_path:
            command = [cmd_path] + command[1:]
        
        # Setup output
        if output_file:
            trace_path = output_file
            if not trace_path.endswith(".trace"):
                trace_path += ".trace"
        else:
            self._temp_dir = tempfile.mkdtemp(prefix="tracesmith_")
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            trace_path = os.path.join(self._temp_dir, f"trace_{timestamp}.trace")
        
        self._trace_file = trace_path
        
        # Build xctrace command
        xctrace_cmd = [
            "xcrun", "xctrace", "record",
            "--template", self.config.template,
            "--output", trace_path,
            "--launch", "--"
        ] + command
        
        if duration:
            xctrace_cmd.insert(4, "--time-limit")
            xctrace_cmd.insert(5, f"{duration}s")
        
        print(f"Starting xctrace profiling...")
        print(f"  Template: {self.config.template}")
        print(f"  Command: {' '.join(command)}")
        if duration:
            print(f"  Duration: {duration}s")
        print(f"  Output: {trace_path}")
        print()
        
        # Run xctrace
        try:
            result = subprocess.run(
                xctrace_cmd,
                capture_output=True,
                text=True,
                timeout=duration + 60 if duration else 3600
            )
            
            if result.returncode != 0:
                # xctrace may return non-zero but still produce a trace
                if not os.path.exists(trace_path):
                    print(f"Warning: xctrace returned {result.returncode}")
                    if result.stderr:
                        print(f"  {result.stderr}")
        except subprocess.TimeoutExpired:
            print("Warning: Command timed out")
        except subprocess.SubprocessError as e:
            print(f"Error running xctrace: {e}")
            return []
        
        # Parse the trace
        if os.path.exists(trace_path):
            print(f"\nParsing trace file...")
            self._events = self._parse_trace(trace_path)
            print(f"  Captured {len(self._events)} events")
        
        return self._events
    
    def _parse_trace(self, trace_path: str) -> List['TraceEvent']:
        """Parse xctrace output into TraceSmith events."""
        events = []
        
        # Get table of contents
        toc = self._get_trace_toc(trace_path)
        if not toc:
            return events
        
        # Parse Metal GPU execution points
        gpu_events = self._export_table(
            trace_path,
            "metal-gpu-execution-points"
        )
        for gpu_event in gpu_events:
            try:
                event = gpu_event.to_trace_event()
                events.append(event)
            except Exception as e:
                pass  # Skip malformed events
        
        # Parse Metal driver intervals
        driver_events = self._export_table(
            trace_path,
            "metal-driver-intervals",
            target_pid="SINGLE"
        )
        for driver_event in driver_events:
            try:
                event = driver_event.to_trace_event()
                events.append(event)
            except Exception:
                pass
        
        # Sort by timestamp
        events.sort(key=lambda e: e.timestamp)
        
        return events
    
    def _get_trace_toc(self, trace_path: str) -> Optional[str]:
        """Get trace table of contents."""
        try:
            result = subprocess.run(
                ["xcrun", "xctrace", "export", "--input", trace_path, "--toc"],
                capture_output=True,
                text=True,
                timeout=30
            )
            if result.returncode == 0:
                return result.stdout
        except subprocess.SubprocessError:
            pass
        return None
    
    def _export_table(
        self,
        trace_path: str,
        schema: str,
        target_pid: Optional[str] = None
    ) -> List[Any]:
        """Export a specific table from the trace."""
        events = []
        
        # Build XPath query
        if target_pid:
            xpath = f'/trace-toc/run[@number="1"]/data/table[@schema="{schema}" and @target-pid="{target_pid}"]'
        else:
            xpath = f'/trace-toc/run[@number="1"]/data/table[@schema="{schema}"]'
        
        try:
            result = subprocess.run(
                ["xcrun", "xctrace", "export",
                 "--input", trace_path,
                 "--xpath", xpath],
                capture_output=True,
                text=True,
                timeout=60
            )
            
            if result.returncode == 0 and result.stdout:
                events = self._parse_xml_table(result.stdout, schema)
                
        except subprocess.SubprocessError as e:
            print(f"Warning: Failed to export {schema}: {e}")
        
        return events
    
    def _parse_xml_table(self, xml_content: str, schema: str) -> List[Any]:
        """Parse XML table content into events."""
        events = []
        
        try:
            root = ET.fromstring(xml_content)
            
            if schema == "metal-gpu-execution-points":
                events = self._parse_gpu_execution_points(root)
            elif schema == "metal-driver-intervals":
                events = self._parse_driver_intervals(root)
                
        except ET.ParseError as e:
            print(f"Warning: XML parse error: {e}")
        
        return events
    
    def _parse_gpu_execution_points(self, root: ET.Element) -> List[MetalGPUEvent]:
        """Parse metal-gpu-execution-points table."""
        events = []
        
        for row in root.findall(".//row"):
            try:
                # Extract timestamp
                timestamp_elem = row.find("start-time")
                if timestamp_elem is None:
                    continue
                
                timestamp_ns = int(timestamp_elem.text or "0")
                
                # Extract other fields
                channel_id = self._get_element_value(row, "metal-command-buffer-id", "0")
                function = int(self._get_element_value(row, "uint32", "0"))
                
                # Find slot-id and gpu-submission-id
                uint32_elems = row.findall("uint32")
                slot_id = int(uint32_elems[1].get("fmt", "0")) if len(uint32_elems) > 1 else 0
                
                submission_elem = row.findall("metal-command-buffer-id")
                gpu_submission_id = submission_elem[1].get("fmt", "0") if len(submission_elem) > 1 else "0"
                
                accelerator_id = int(self._get_element_value(row, "uint64", "0"))
                note = self._get_element_value(row, "string", "")
                
                event = MetalGPUEvent(
                    timestamp_ns=timestamp_ns,
                    channel_id=channel_id,
                    function=function,
                    slot_id=slot_id,
                    gpu_submission_id=gpu_submission_id,
                    accelerator_id=accelerator_id,
                    note=note
                )
                events.append(event)
                
            except (ValueError, AttributeError) as e:
                continue
        
        return events
    
    def _parse_driver_intervals(self, root: ET.Element) -> List[MetalDriverInterval]:
        """Parse metal-driver-intervals table."""
        events = []
        
        for row in root.findall(".//row"):
            try:
                start_elem = row.find("start-time")
                duration_elem = row.find("duration")
                name_elem = row.find("string")
                
                if start_elem is None:
                    continue
                
                start_ns = int(start_elem.text or "0")
                duration_ns = int(duration_elem.text or "0") if duration_elem is not None else 0
                name = name_elem.text or "unknown" if name_elem is not None else "unknown"
                
                event = MetalDriverInterval(
                    start_ns=start_ns,
                    duration_ns=duration_ns,
                    name=name
                )
                events.append(event)
                
            except (ValueError, AttributeError):
                continue
        
        return events
    
    def _get_element_value(self, row: ET.Element, tag: str, default: str = "") -> str:
        """Get element value or attribute from row."""
        elem = row.find(tag)
        if elem is not None:
            # Try fmt attribute first, then text
            return elem.get("fmt", elem.text or default)
        return default
    
    def get_events(self) -> List['TraceEvent']:
        """Get captured events."""
        return self._events
    
    def export_perfetto(self, output_file: str) -> bool:
        """Export events to Perfetto format."""
        if not self._events:
            print("No events to export")
            return False
        
        try:
            return export_perfetto(self._events, output_file)
        except Exception as e:
            print(f"Export failed: {e}")
            return False
    
    def get_trace_file(self) -> Optional[str]:
        """Get path to the raw .trace file."""
        return self._trace_file
    
    def cleanup(self):
        """Clean up temporary files."""
        if self._temp_dir and os.path.exists(self._temp_dir):
            shutil.rmtree(self._temp_dir, ignore_errors=True)
            self._temp_dir = None
    
    def __del__(self):
        """Destructor to clean up."""
        self.cleanup()


def profile_with_xctrace(
    command: List[str],
    duration: Optional[int] = None,
    template: str = "Metal System Trace",
    output_file: Optional[str] = None
) -> Tuple[List['TraceEvent'], Optional[str]]:
    """
    Convenience function to profile a command with xctrace.
    
    Args:
        command: Command and arguments to profile
        duration: Maximum duration in seconds
        template: Instruments template to use
        output_file: Optional output file path
        
    Returns:
        Tuple of (events list, trace file path)
    """
    config = XCTraceConfig(template=template, duration_seconds=duration or 60)
    profiler = XCTraceProfiler(config)
    
    events = profiler.profile_command(command, duration, output_file)
    trace_file = profiler.get_trace_file()
    
    return events, trace_file


# CLI integration helper
def add_xctrace_arguments(parser):
    """Add xctrace-specific arguments to an argument parser."""
    parser.add_argument(
        "--xctrace",
        action="store_true",
        help="Use Apple Instruments (xctrace) for Metal GPU profiling on macOS"
    )
    parser.add_argument(
        "--xctrace-template",
        default="Metal System Trace",
        help="Instruments template (default: 'Metal System Trace')"
    )
    parser.add_argument(
        "--keep-trace",
        action="store_true",
        help="Keep the raw .trace file after profiling"
    )


if __name__ == "__main__":
    # Test the module
    print("XCTrace Integration Test")
    print("=" * 60)
    
    if not XCTraceProfiler.is_available():
        print("xctrace not available on this system")
        sys.exit(1)
    
    print(f"xctrace available: Yes")
    print()
    
    print("Available Metal templates:")
    for template in XCTraceProfiler.get_metal_templates():
        print(f"  - {template}")
    print()
    
    # Simple test
    print("Running test profile...")
    events, trace_file = profile_with_xctrace(
        ["python3", "-c", "print('Hello from xctrace test')"],
        duration=3
    )
    
    print(f"\nResults:")
    print(f"  Events captured: {len(events)}")
    print(f"  Trace file: {trace_file}")
