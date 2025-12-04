"""
TraceSmith Command Line Interface

Usage:
    tracesmith info              Show version and system info
    tracesmith convert FILE      Convert trace files
    tracesmith analyze FILE      Analyze trace file
    tracesmith export FILE       Export to Perfetto format
"""

import argparse
import sys
from pathlib import Path


def get_version():
    """Get TraceSmith version."""
    try:
        from . import __version__
        return __version__
    except ImportError:
        return "unknown"


def cmd_info(args):
    """Show version and system information."""
    from . import (
        __version__,
        VERSION_MAJOR,
        VERSION_MINOR,
        VERSION_PATCH,
        is_protobuf_available,
        is_bpf_available,
    )
    
    print(f"TraceSmith v{__version__}")
    print(f"  Version: {VERSION_MAJOR}.{VERSION_MINOR}.{VERSION_PATCH}")
    print()
    print("Features:")
    print(f"  Perfetto Protobuf: {'✓' if is_protobuf_available() else '✗'}")
    print(f"  BPF Tracing:       {'✓' if is_bpf_available() else '✗ (Linux only)'}")
    print()
    print("Supported Platforms:")
    print("  • CUDA (NVIDIA)")
    print("  • ROCm (AMD)")
    print("  • Metal (Apple)")
    print("  • Simulation (Cross-platform)")
    return 0


def cmd_convert(args):
    """Convert trace files between formats."""
    from . import SBTReader, PerfettoExporter, PerfettoProtoExporter, PerfettoFormat
    
    input_path = Path(args.input)
    output_path = Path(args.output) if args.output else input_path.with_suffix('.json')
    
    if not input_path.exists():
        print(f"Error: Input file '{input_path}' not found", file=sys.stderr)
        return 1
    
    # Read SBT file
    if input_path.suffix == '.sbt':
        reader = SBTReader(str(input_path))
        if not reader.is_valid():
            print(f"Error: Invalid SBT file '{input_path}'", file=sys.stderr)
            return 1
        metadata, events = reader.read_all()
        print(f"Read {len(events)} events from {input_path}")
    else:
        print(f"Error: Unsupported input format '{input_path.suffix}'", file=sys.stderr)
        return 1
    
    # Export
    if args.protobuf or output_path.suffix in ('.perfetto-trace', '.pftrace'):
        exporter = PerfettoProtoExporter(PerfettoFormat.PROTOBUF)
        output_path = output_path.with_suffix('.perfetto-trace')
    else:
        exporter = PerfettoExporter()
    
    if exporter.export_to_file(events, str(output_path)):
        print(f"Exported to {output_path}")
        return 0
    else:
        print(f"Error: Failed to export to '{output_path}'", file=sys.stderr)
        return 1


def cmd_analyze(args):
    """Analyze a trace file."""
    from . import SBTReader, build_timeline
    
    input_path = Path(args.input)
    
    if not input_path.exists():
        print(f"Error: Input file '{input_path}' not found", file=sys.stderr)
        return 1
    
    # Read file
    if input_path.suffix == '.sbt':
        reader = SBTReader(str(input_path))
        if not reader.is_valid():
            print(f"Error: Invalid SBT file '{input_path}'", file=sys.stderr)
            return 1
        metadata, events = reader.read_all()
    else:
        print(f"Error: Unsupported format '{input_path.suffix}'", file=sys.stderr)
        return 1
    
    # Build timeline
    timeline = build_timeline(events)
    
    print(f"Trace Analysis: {input_path}")
    print("=" * 60)
    print(f"Total Events:        {len(events)}")
    print(f"Timeline Spans:      {len(timeline.spans)}")
    print(f"Total Duration:      {timeline.total_duration / 1e6:.2f} ms")
    print(f"GPU Utilization:     {timeline.gpu_utilization * 100:.1f}%")
    print(f"Max Concurrent Ops:  {timeline.max_concurrent_ops}")
    
    # Event type breakdown
    from collections import Counter
    type_counts = Counter(e.type for e in events)
    
    print()
    print("Event Types:")
    for event_type, count in type_counts.most_common():
        from . import event_type_to_string
        print(f"  {event_type_to_string(event_type):20s} {count:6d}")
    
    return 0


def cmd_export(args):
    """Export trace to Perfetto format."""
    return cmd_convert(args)


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        prog='tracesmith',
        description='TraceSmith GPU Profiling & Replay System',
    )
    parser.add_argument('--version', action='version', version=f'%(prog)s {get_version()}')
    
    subparsers = parser.add_subparsers(dest='command', help='Commands')
    
    # info command
    info_parser = subparsers.add_parser('info', help='Show version and system info')
    info_parser.set_defaults(func=cmd_info)
    
    # convert command
    convert_parser = subparsers.add_parser('convert', help='Convert trace files')
    convert_parser.add_argument('input', help='Input trace file')
    convert_parser.add_argument('-o', '--output', help='Output file')
    convert_parser.add_argument('--protobuf', action='store_true', help='Use protobuf format')
    convert_parser.set_defaults(func=cmd_convert)
    
    # analyze command
    analyze_parser = subparsers.add_parser('analyze', help='Analyze trace file')
    analyze_parser.add_argument('input', help='Input trace file')
    analyze_parser.set_defaults(func=cmd_analyze)
    
    # export command (alias for convert)
    export_parser = subparsers.add_parser('export', help='Export to Perfetto format')
    export_parser.add_argument('input', help='Input trace file')
    export_parser.add_argument('-o', '--output', help='Output file')
    export_parser.add_argument('--protobuf', action='store_true', help='Use protobuf format')
    export_parser.set_defaults(func=cmd_export)
    
    args = parser.parse_args()
    
    if args.command is None:
        parser.print_help()
        return 0
    
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())

