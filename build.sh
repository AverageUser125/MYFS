#!/bin/bash
# Exit immediately if a command exits with a non-zero status.
set -e
# Define the build directory
BUILD_DIR="build"
# Create the build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    mkdir "$BUILD_DIR"
fi
# Run CMake to configure the project
cmake -S . -G "Unix Makefiles" -B "$BUILD_DIR"
# Build the project
cd "$BUILD_DIR"
make
cd ..
# Print a colored success message
YELLOW='\033[0;33m'
NC='\033[0m' # No Color
echo -e "${YELLOW}Build completed successfully."
