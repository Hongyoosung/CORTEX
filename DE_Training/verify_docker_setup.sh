#!/bin/bash
# Docker Setup Verification Script for DE v10.2

echo "=================================================="
echo "  DE v10.2 Docker Setup Verification"
echo "=================================================="
echo ""

# Color codes
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check counter
CHECKS_PASSED=0
CHECKS_FAILED=0
CHECKS_WARNING=0

check_pass() {
    echo -e "${GREEN}✓${NC} $1"
    ((CHECKS_PASSED++))
}

check_fail() {
    echo -e "${RED}✗${NC} $1"
    ((CHECKS_FAILED++))
}

check_warn() {
    echo -e "${YELLOW}⚠${NC} $1"
    ((CHECKS_WARNING++))
}

echo "1. Checking Required Files..."
echo "================================"

# Check Dockerfile
if [ -f "Dockerfile" ]; then
    if grep -q "v10.2" Dockerfile; then
        check_pass "Dockerfile (updated for v10.2)"
    else
        check_warn "Dockerfile (exists but may be outdated)"
    fi
else
    check_fail "Dockerfile (missing)"
fi

# Check docker-compose.yml
if [ -f "docker-compose.yml" ]; then
    if grep -q "training-v10.2" docker-compose.yml; then
        check_pass "docker-compose.yml (v10.2 services configured)"
    else
        check_fail "docker-compose.yml (missing v10.2 services)"
    fi
else
    check_fail "docker-compose.yml (missing)"
fi

# Check requirements.txt
if [ -f "requirements.txt" ]; then
    check_pass "requirements.txt"
else
    check_fail "requirements.txt (missing)"
fi

# Check training scripts
if [ -f "training/policy_training.py" ]; then
    check_pass "policy_training.py"
else
    check_fail "policy_training.py (missing)"
fi

if [ -f "training/de_env.py" ]; then
    check_pass "de_env.py"
else
    check_fail "de_env.py (missing)"
fi

# Check documentation
if [ -f "DOCKER_QUICK_START_.md" ]; then
    check_pass "DOCKER_QUICK_START_.md"
else
    check_warn "DOCKER_QUICK_START_.md (missing)"
fi

if [ -f "training/README_.md" ]; then
    check_pass "training/README_.md"
else
    check_warn "training/README_.md (missing)"
fi

echo ""
echo "2. Checking Docker Installation..."
echo "================================"

# Check Docker
if command -v docker &> /dev/null; then
    DOCKER_VERSION=$(docker --version)
    check_pass "Docker installed: $DOCKER_VERSION"
    
    # Check if Docker daemon is running
    if docker info &> /dev/null; then
        check_pass "Docker daemon running"
    else
        check_fail "Docker daemon not running (start Docker Desktop)"
    fi
else
    check_fail "Docker not installed"
fi

# Check Docker Compose
if command -v docker-compose &> /dev/null; then
    COMPOSE_VERSION=$(docker-compose --version)
    check_pass "Docker Compose installed: $COMPOSE_VERSION"
else
    check_fail "Docker Compose not installed"
fi

echo ""
echo "3. Checking Directory Structure..."
echo "================================"

# Check parent directories
if [ -d "../Plugins/Schola-1.3.0" ]; then
    check_pass "Schola plugin directory found"
else
    check_warn "Schola plugin directory not found (build may fail)"
fi

# Check output directories (will be created if missing)
if [ -d "training_results_" ]; then
    check_pass "training_results_ directory exists"
else
    check_warn "training_results_ directory missing (will be auto-created)"
fi

if [ -d "models" ]; then
    check_pass "models directory exists"
else
    check_warn "models directory missing (will be auto-created)"
fi

echo ""
echo "4. Checking Docker Configuration..."
echo "================================"

# Check docker-compose services
if [ -f "docker-compose.yml" ]; then
    SERVICES=$(docker-compose config --services 2>/dev/null)
    if echo "$SERVICES" | grep -q "training-v10.2"; then
        check_pass "training-v10.2 service configured"
    else
        check_fail "training-v10.2 service not found"
    fi
    
    if echo "$SERVICES" | grep -q "tensorboard-v10.2"; then
        check_pass "tensorboard-v10.2 service configured"
    else
        check_warn "tensorboard-v10.2 service not found"
    fi
fi

echo ""
echo "5. Checking Network Connectivity..."
echo "================================"

# Check if port 50051 is available or in use
if command -v netstat &> /dev/null; then
    if netstat -an | grep -q "50051"; then
        check_pass "Port 50051 in use (UE5 likely running)"
    else
        check_warn "Port 50051 not in use (start UE5 before training)"
    fi
else
    check_warn "Cannot check port status (netstat not available)"
fi

echo ""
echo "=================================================="
echo "  Verification Summary"
echo "=================================================="
echo -e "${GREEN}Passed:${NC}   $CHECKS_PASSED"
echo -e "${YELLOW}Warnings:${NC} $CHECKS_WARNING"
echo -e "${RED}Failed:${NC}   $CHECKS_FAILED"
echo ""

if [ $CHECKS_FAILED -eq 0 ]; then
    echo -e "${GREEN}✓ Docker setup is ready!${NC}"
    echo ""
    echo "Next steps:"
    echo "1. Start UE5 in PIE mode"
    echo "2. Run: docker-compose --profile v10.2 up --build training-v10.2"
    echo ""
    exit 0
else
    echo -e "${RED}✗ Some checks failed. Please fix the issues above.${NC}"
    echo ""
    exit 1
fi
