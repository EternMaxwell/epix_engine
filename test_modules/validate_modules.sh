#!/bin/bash
set -e

echo "========================================="
echo "Validating C++20 Module Syntax"
echo "========================================="
echo ""

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

COMPILER="${CXX:-g++}"
CXXFLAGS="-std=c++23 -fmodules-ts -fsyntax-only"
INCLUDES="-I../epix_engine/core/include -I../epix_engine/assets/include -I../epix_engine/window/include -I../epix_engine/input/include -I../epix_engine/transform/include -I../epix_engine/image/include -I../epix_engine/sprite/include -I../epix_engine/render/include -I../libs/spdlog/include -I../libs/glm -I../libs/uuid/include -I../libs/nvrhi/include -I../libs/entt/single_include -I../libs/thread-pool/include -I../libs/glfw/include"

PASS_COUNT=0
FAIL_COUNT=0
TOTAL_COUNT=0

validate_module() {
    local module_file=$1
    local module_name=$(basename "$module_file")
    
    echo -n "Validating ${module_name}... "
    TOTAL_COUNT=$((TOTAL_COUNT + 1))
    
    if $COMPILER $CXXFLAGS $INCLUDES "$module_file" 2>&1 | grep -q "error:"; then
        echo -e "${RED}FAIL${NC}"
        $COMPILER $CXXFLAGS $INCLUDES "$module_file" 2>&1 | head -20
        FAIL_COUNT=$((FAIL_COUNT + 1))
        return 1
    else
        echo -e "${GREEN}PASS${NC}"
        PASS_COUNT=$((PASS_COUNT + 1))
        return 0
    fi
}

echo "Core Module Partitions:"
echo "-----------------------"
for module in ../epix_engine/core/modules/*.cppm; do
    validate_module "$module"
done

echo ""
echo "Image Module:"
echo "-------------"
for module in ../epix_engine/image/modules/*.cppm; do
    if [ -f "$module" ]; then
        validate_module "$module"
    fi
done

echo ""
echo "Assets Module:"
echo "--------------"
for module in ../epix_engine/assets/modules/*.cppm; do
    if [ -f "$module" ]; then
        validate_module "$module"
    fi
done

echo ""
echo "Window Module:"
echo "--------------"
for module in ../epix_engine/window/modules/*.cppm; do
    if [ -f "$module" ]; then
        validate_module "$module"
    fi
done

echo ""
echo "Input Module:"
echo "-------------"
for module in ../epix_engine/input/modules/*.cppm; do
    if [ -f "$module" ]; then
        validate_module "$module"
    fi
done

echo ""
echo "Transform Module:"
echo "-----------------"
for module in ../epix_engine/transform/modules/*.cppm; do
    if [ -f "$module" ]; then
        validate_module "$module"
    fi
done

echo ""
echo "Sprite Module:"
echo "--------------"
for module in ../epix_engine/sprite/modules/*.cppm; do
    if [ -f "$module" ]; then
        validate_module "$module"
    fi
done

echo ""
echo "Render Module:"
echo "--------------"
for module in ../epix_engine/render/modules/*.cppm; do
    if [ -f "$module" ]; then
        validate_module "$module"
    fi
done

echo ""
echo "========================================="
echo "Module Validation Summary"
echo "========================================="
echo "Total modules tested: $TOTAL_COUNT"
echo -e "Passed: ${GREEN}${PASS_COUNT}${NC}"
echo -e "Failed: ${RED}${FAIL_COUNT}${NC}"
echo ""

if [ $FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}All modules validated successfully!${NC}"
    exit 0
else
    echo -e "${RED}Some modules failed validation.${NC}"
    exit 1
fi
