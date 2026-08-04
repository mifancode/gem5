#!/bin/bash
# InstRegTrace Viewer - simple web server
# Usage: ./serve.sh [port]

PORT=${1:-9555}
DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Serving InstRegTrace Viewer at http://localhost:$PORT"
echo "Directory: $DIR"
echo "Press Ctrl+C to stop"

python3 -m http.server "$PORT" -d "$DIR"
