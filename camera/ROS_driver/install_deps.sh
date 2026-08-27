#!/bin/bash
# ==============================================================================
# MVS ROS2 Driver — Dependency Install Script
# ==============================================================================
# Idempotent — safe to run multiple times.
#
# Usage:
#   chmod +x install_deps.sh
#   ./install_deps.sh               # CPU-only (this machine) — OpenCV CPU + MVS SDK
#   ./install_deps.sh --gpu         # GPU mode — CUDA + TensorRT + jetson-utils (kodifly)
#   ./install_deps.sh --build       # CPU mode + colcon build after deps
#   ./install_deps.sh --gpu --build # GPU mode + colcon build
#
# What it does:
#   1. Installs ROS2 Humble (if not already present)
#   2. Installs system build tools and libraries
#   3. Installs ROS2 packages via rosdep
#   4. Sets up MVS SDK udev rules for camera USB access
#   5. [GPU mode] Installs CUDA, TensorRT, jetson-utils
#   6. Runs rosdep check + colcon build (optional, --build flag)
# ==============================================================================

set -euo pipefail

# --- Color helpers ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC}  $*"; }
err()  { echo -e "${RED}[ERROR]${NC} $*"; }
step() { echo -e "\n${BLUE}==== $* ====${NC}\n"; }

# --- Parse flags ---
GPU_MODE=false
DO_BUILD=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --gpu)    GPU_MODE=true; shift ;;
        --build)  DO_BUILD=true; shift ;;
        --help|-h)
            echo "Usage: $0 [--gpu] [--build]"
            echo "  --gpu     Include CUDA, TensorRT, jetson-utils (for kodifly machine)"
            echo "  --build   Run colcon build after installing deps"
            echo ""
            echo "Build modes:"
            echo "  CPU (default): colcon build --cmake-args -DBUILD_CPU_ONLY=ON"
            echo "  GPU (--gpu):   colcon build (full CUDA/TensorRT/YOLOv8)"
            exit 0 ;;
        *) err "Unknown flag: $1"; exit 1 ;;
    esac
done

# --- Detect platform ---
ARCH=$(uname -m)
OS_CODENAME=$(lsb_release -cs 2>/dev/null || echo "unknown")
IS_NVIDIA=false
if lspci 2>/dev/null | grep -qi nvidia; then
    IS_NVIDIA=true
fi

log "Platform: $ARCH, OS: $OS_CODENAME"
log "NVIDIA GPU detected: $IS_NVIDIA"
log "GPU mode: $GPU_MODE"

if $GPU_MODE && ! $IS_NVIDIA; then
    warn "--gpu specified but no NVIDIA GPU detected. GPU packages will fail to install."
    warn "Proceeding anyway in case the target is a different machine."
fi

# ==============================================================================
# Step 1: System build tools
# ==============================================================================
step "Step 1: System build tools"

sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    python3-pip \
    python3-colcon-common-extensions \
    python3-rosdep \
    python3-vcstool \
    lsb-release \
    pkg-config \
    libpcl-dev \
    libopencv-dev

log "Build tools installed."

# ==============================================================================
# Step 2: ROS2 Humble
# ==============================================================================
step "Step 2: ROS2 Humble"

if dpkg -s ros-humble-ros-base &>/dev/null; then
    log "ROS2 Humble already installed."
else
    log "Installing ROS2 Humble..."

    # Only add the apt repo if it's not already configured
    if ! grep -q "packages.ros.org/ros2" /etc/apt/sources.list.d/*.list 2>/dev/null; then
        # Clean up any old ROS1 or duplicate ROS2 apt sources to avoid GPG key conflicts
        sudo rm -f /etc/apt/sources.list.d/ros-latest.list
        sudo rm -f /etc/apt/sources.list.d/ros2.list

        sudo apt-get install -y software-properties-common
        sudo add-apt-repository -y universe
        sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
            -o /usr/share/keyrings/ros-archive-keyring.gpg
        echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu $OS_CODENAME main" | \
            sudo tee /etc/apt/sources.list.d/ros2.list > /dev/null
        sudo apt-get update -qq
    else
        log "ROS2 apt repository already configured."
        # Still clean old ros-latest.list (ROS1) to avoid GPG conflicts
        sudo rm -f /etc/apt/sources.list.d/ros-latest.list
        sudo apt-get update -qq || log "apt update had warnings — continuing"
    fi

    sudo apt-get install -y ros-humble-ros-base
    log "ROS2 Humble installed."
fi

# Source ROS2 for this script
if [ -f /opt/ros/humble/setup.bash ]; then
    source /opt/ros/humble/setup.bash
else
    warn "/opt/ros/humble/setup.bash not found — continuing without sourcing"
fi

# ==============================================================================
# Step 3: ROS2 package dependencies
# ==============================================================================
step "Step 3: ROS2 package dependencies"

ROS2_PACKAGES=(
    ros-humble-rclcpp
    ros-humble-cv-bridge
    ros-humble-image-transport
    ros-humble-sensor-msgs
    ros-humble-std-msgs
    ros-humble-ament-cmake
    ros-humble-launch-ros
    ros-humble-rosbag2-cpp
)

MISSING_PKGS=()
for pkg in "${ROS2_PACKAGES[@]}"; do
    if ! dpkg -l | grep -q "^ii.*$(echo $pkg | sed 's/.*-//' | head -c 30)"; then
        # More reliable check: use dpkg -s
        if ! dpkg -s "$pkg" &>/dev/null; then
            MISSING_PKGS+=("$pkg")
        fi
    fi
done

if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
    log "Installing missing ROS2 packages: ${MISSING_PKGS[*]}"
    sudo apt-get install -y "${MISSING_PKGS[@]}"
else
    log "All ROS2 packages already installed."
fi

# ==============================================================================
# Step 4: Initialize rosdep
# ==============================================================================
step "Step 4: rosdep"

if [ ! -f /etc/ros/rosdep/sources.list.d/20-default.list ]; then
    log "Initializing rosdep..."
    sudo rosdep init || log "rosdep already initialized (warning above is OK)"
fi

rosdep update || log "rosdep update had warnings — continuing"

# ==============================================================================
# Step 5: MVS SDK udev rules (for USB camera access)
# ==============================================================================
step "Step 5: MVS SDK udev rules"

MVS_SCRIPT="/opt/MVS/bin/set_usb_priority.sh"
if [ -f "$MVS_SCRIPT" ]; then
    log "Found MVS SDK at /opt/MVS"
    log "Setting up udev rules for Hikrobot camera (vendor 0x2bdf)..."
    sudo "$MVS_SCRIPT" --add=2bdf || log "udev rules may already be set (error above is OK)"
    log "If the camera was plugged in during this step, unplug/replug it now."
else
    warn "MVS SDK not found at /opt/MVS."
    warn "Download from Hikrobot and install to /opt/MVS before building."
fi

# ==============================================================================
# Step 6: GPU-specific dependencies (only with --gpu)
# ==============================================================================
if $GPU_MODE; then
    step "Step 6: GPU dependencies (CUDA, TensorRT, jetson-utils)"

    # --- CUDA 12.x ---
    if nvcc --version &>/dev/null; then
        log "CUDA already installed: $(nvcc --version | grep release)"
    else
        log "Installing CUDA 12.1..."
        wget -q https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb -O /tmp/cuda-keyring.deb
        sudo dpkg -i /tmp/cuda-keyring.deb
        sudo apt-get update -qq
        sudo apt-get install -y cuda-toolkit-12-1
        rm -f /tmp/cuda-keyring.deb
        log "CUDA 12.1 installed."
    fi

    # --- TensorRT ---
    if dpkg -l | grep -q "libnvinfer"; then
        log "TensorRT already installed."
    else
        log "Installing TensorRT..."
        # TensorRT requires the NVIDIA CUDA repository already set up
        sudo apt-get install -y \
            libnvinfer-dev \
            libnvinfer-plugin-dev \
            libnvonnxparsers-dev \
            libnvparsers-dev \
            || warn "TensorRT apt install failed. May need manual install from NVIDIA SDK Manager."
    fi

    # --- jetson-utils ---
    # Note: jetson-utils is a Jetson-specific library. On x86_64 with discrete GPU,
    # the debayering functions (cudaBayerToRGB) must come from a port or alternative.
    if dpkg -l | grep -q "jetson-utils"; then
        log "jetson-utils already installed."
    else
        warn "jetson-utils is typically only available on Jetson (aarch64) platforms."
        warn "On x86_64 with RTX 3080, you may need a custom port."
        warn "See: https://github.com/dusty-nv/jetson-utils"
        warn "Skipping jetson-utils — build will fail until it is provided."
    fi

    # --- GPU-enabled OpenCV ---
    warn "The CMakeLists.txt expects custom OpenCV at /home/kodifly/libraries/opencv/build"
    warn "This is a CUDA-enabled OpenCV build. The system OpenCV (CPU-only) will NOT work."
    warn "If recompiling on a different machine, update OpenCV_DIR in CMakeLists.txt."
fi

# ==============================================================================
# Step 7: rosdep install for the package
# ==============================================================================
step "Step 7: rosdep install (package dependencies)"

# Determine workspace root — assume this script is in or near the workspace
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Try to find the workspace root (look for mvs_ros_driver)
if [ -d "$SCRIPT_DIR/mvs_ros_driver" ]; then
    WS_ROOT="$SCRIPT_DIR"
elif [ -d "$SCRIPT_DIR/../mvs_ros_driver" ]; then
    WS_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
else
    WS_ROOT="$SCRIPT_DIR"
    warn "Could not locate mvs_ros_driver. Set WS_ROOT manually."
fi

log "Workspace root: $WS_ROOT"

if [ -f "$WS_ROOT/mvs_ros_driver/package.xml" ]; then
    log "Running rosdep install for mvs_ros_driver..."
    cd "$WS_ROOT"
    rosdep install --from-paths mvs_ros_driver --ignore-src -y \
        --skip-keys="MvCameraControl jetson-utils tensorrt_cpp_api YoloV8_TRT" \
        || warn "rosdep reported issues — some system deps (CUDA/TensorRT/MVS) are expected to fail on CPU-only machines."
else
    warn "mvs_ros_driver/package.xml not found under $WS_ROOT — skipping rosdep"
fi

# ==============================================================================
# Step 8: Environment setup reminder
# ==============================================================================
step "Step 8: Environment setup"

echo ""
log "Add these lines to your ~/.bashrc (or run them now):"
echo ""
echo "  # ROS2 Humble"
echo "  source /opt/ros/humble/setup.bash"
echo ""
echo "  # MVS SDK"
echo "  export LD_LIBRARY_PATH=/opt/MVS/bin:/opt/MVS/lib/64:/opt/MVS/lib/32:\$LD_LIBRARY_PATH"
echo ""

if $GPU_MODE; then
    echo "  # CUDA"
    echo "  export PATH=/usr/local/cuda/bin:\$PATH"
    echo "  export LD_LIBRARY_PATH=/usr/local/cuda/lib64:\$LD_LIBRARY_PATH"
    echo ""
fi

# ==============================================================================
# Step 9: Build (only with --build)
# ==============================================================================
if $DO_BUILD; then
    step "Step 9: Building with colcon"

    cd "$WS_ROOT"
    if [ -f /opt/ros/humble/setup.bash ]; then
        source /opt/ros/humble/setup.bash
    fi

    if $GPU_MODE; then
        log "Building GPU mode (full CUDA/TensorRT/YOLOv8)..."
        colcon build --packages-select mvs_ros_driver --symlink-install 2>&1 || {
            warn "GPU build failed. Check CUDA, TensorRT, jetson-utils, custom OpenCV paths."
        }
    else
        log "Building CPU-only mode (OpenCV CPU + MVS SDK)..."
        colcon build --packages-select mvs_ros_driver --symlink-install \
            --cmake-args -DBUILD_CPU_ONLY=ON 2>&1 || {
            warn "CPU build failed. Check that OpenCV and MVS SDK are installed."
        }
    fi
fi

# ==============================================================================
# Done
# ==============================================================================
echo ""
log "============================================="
log "Install script complete."
log ""
log "Summary:"
log "  ROS2 Humble:    installed"
log "  ROS2 packages:  installed"
log "  MVS SDK udev:   configured (if SDK present)"
if $GPU_MODE; then
    log "  GPU deps:       CUDA / TensorRT / jetson-utils attempted"
    log "  Build mode:     GPU (grabImgWithTrigger)"
else
    log "  GPU deps:       skipped (CPU-only mode)"
    log "  Build mode:     CPU-only (grabImgWithTriggerCPU)"
fi
log ""
log "Next steps:"
log "  1. source /opt/ros/humble/setup.bash"
log "  2. source install/setup.bash  # (or install/local_setup.bash)"
log "  3. export LD_LIBRARY_PATH=/opt/MVS/bin:/opt/MVS/lib/64:\$LD_LIBRARY_PATH"
if $GPU_MODE; then
    log "  4. ros2 launch mvs_ros_driver hikrobot.launch.py"
else
    log "  4. ros2 launch mvs_ros_driver test_cpu.launch.py"
fi
log "============================================="
