#!/bin/bash
# TraceSmith Metal Profiling Script
# Uses Apple Instruments for Metal GPU analysis on macOS

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${CYAN}"
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║  TraceSmith - Metal GPU Profiling (via Instruments)       ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo -e "${NC}"

# Default values
DURATION=10
OUTPUT_DIR="./metal_traces"
TEMPLATE="Metal System Trace"

# Parse arguments
show_help() {
    echo "Usage: $0 [OPTIONS] -- COMMAND [ARGS...]"
    echo ""
    echo "Options:"
    echo "  -d, --duration SEC    Recording duration (default: 10s)"
    echo "  -o, --output DIR      Output directory (default: ./metal_traces)"
    echo "  -t, --template NAME   Instruments template (default: 'Metal System Trace')"
    echo "  -l, --list            List available templates"
    echo "  -h, --help            Show this help"
    echo ""
    echo "Examples:"
    echo "  $0 -- python train.py"
    echo "  $0 -d 30 -- python inference.py --batch-size 32"
    echo "  $0 -t 'GPU Driver' -- ./my_metal_app"
    echo ""
    echo "Available Metal templates:"
    echo "  - 'Metal System Trace'  (most detailed)"
    echo "  - 'GPU Driver'"
    echo "  - 'Game Performance'"
    echo "  - 'Animation Hitches'"
}

list_templates() {
    echo -e "${CYAN}Available Instruments Templates:${NC}"
    xcrun xctrace list templates | grep -E "(Metal|GPU|Game|Animation)" || echo "  (run 'xcrun xctrace list templates' for all)"
}

COMMAND=""
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--duration)
            DURATION="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -t|--template)
            TEMPLATE="$2"
            shift 2
            ;;
        -l|--list)
            list_templates
            exit 0
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        --)
            shift
            COMMAND="$@"
            break
            ;;
        *)
            COMMAND="$@"
            break
            ;;
    esac
done

if [ -z "$COMMAND" ]; then
    echo -e "${RED}Error: No command specified${NC}"
    echo ""
    show_help
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Generate output filename
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
CMD_NAME=$(echo "$COMMAND" | awk '{print $1}' | xargs basename)
TRACE_FILE="${OUTPUT_DIR}/${CMD_NAME}_${TIMESTAMP}.trace"

echo -e "${GREEN}Configuration:${NC}"
echo "  Command:   $COMMAND"
echo "  Duration:  ${DURATION}s"
echo "  Template:  $TEMPLATE"
echo "  Output:    $TRACE_FILE"
echo ""

# Check if xctrace is available
if ! command -v xcrun &> /dev/null; then
    echo -e "${RED}Error: Xcode command line tools not found${NC}"
    echo "Install with: xcode-select --install"
    exit 1
fi

echo -e "${CYAN}Starting Metal profiling...${NC}"
echo "Press Ctrl+C to stop early"
echo ""

# Run xctrace
xcrun xctrace record \
    --template "$TEMPLATE" \
    --output "$TRACE_FILE" \
    --time-limit "${DURATION}s" \
    --launch -- $COMMAND

echo ""
echo -e "${GREEN}═══════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}Profiling Complete!${NC}"
echo ""
echo -e "Trace saved to: ${CYAN}$TRACE_FILE${NC}"
echo ""
echo "To analyze:"
echo -e "  ${YELLOW}open \"$TRACE_FILE\"${NC}  # Open in Instruments"
echo ""
echo "To export data:"
echo -e "  ${YELLOW}xcrun xctrace export --input \"$TRACE_FILE\" --output \"${TRACE_FILE%.trace}\"${NC}"
echo ""

# Open the trace file
read -p "Open trace in Instruments? [Y/n] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]] || [[ -z $REPLY ]]; then
    open "$TRACE_FILE"
fi
