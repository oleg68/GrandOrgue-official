#!/bin/bash

# $1 - optional - [test | coverage | gcovr] - If nothing is provided,
# run all the tests steps.

set -e

STEP=$1

# Print system information for performance test comparison
echo "=========================================="
echo "System Information for Performance Tests"
echo "=========================================="
echo ""

# CPU Model
echo "=== CPU Model ==="
cat /proc/cpuinfo | grep "model name" | head -1 || echo "CPU model: N/A"
echo ""

# CPU Architecture and Cores
echo "=== CPU Architecture & Cores ==="
lscpu | grep -E "Architecture:|CPU\(s\):|Thread\(s\) per core:|Core\(s\) per socket:" || echo "CPU info: N/A"
echo ""

# CPU Frequency
echo "=== CPU Frequency ==="
lscpu | grep -E "MHz|max MHz|min MHz" || echo "CPU frequency: N/A"
echo ""

# CPU Cache
echo "=== CPU Cache ==="
lscpu | grep -E "L1d cache:|L1i cache:|L2 cache:|L3 cache:" || echo "Cache info: N/A"
echo ""

# CPU Flags (AVX support)
echo "=== CPU SIMD Support ==="
grep -o "avx[^ ]*\|sse[^ ]*" /proc/cpuinfo | head -20 | sort -u | tr '\n' ' ' || echo "SIMD flags: N/A"
echo ""
echo ""

# GCC Version
echo "=== GCC Version ==="
gcc --version | head -1 || echo "GCC: N/A"
echo ""

# CPU Governor
echo "=== CPU Governor ==="
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | uniq || echo "Governor: N/A (not available)"
echo ""

# RAM
echo "=== Memory ==="
free -h | grep Mem || echo "Memory info: N/A"
echo ""

# OS Information
echo "=== Operating System ==="
if [ -f /etc/os-release ]; then
    grep -E "PRETTY_NAME|VERSION_ID" /etc/os-release || echo "OS: N/A"
else
    echo "OS: N/A"
fi
echo ""

# Hostname
echo "=== Hostname ==="
hostname || echo "Hostname: N/A"
echo ""

echo "=========================================="
echo "Starting Tests"
echo "=========================================="
echo ""

TEST_SCRIPT="ctest -T test --verbose"
COVERAGE_SCRIPT="ctest -T coverage"
GCOVR_SCRIPT="gcovr -e 'submodules/*|usr/*'"

case $STEP in
    "test")
        $TEST_SCRIPT
    ;;
    "coverage")
        $COVERAGE_SCRIPT
    ;;
    "gcovr")
        $GCOVR_SCRIPT
    ;;
    "")
        $TEST_SCRIPT ; $COVERAGE_SCRIPT ; $GCOVR_SCRIPT
esac;


