```bash
#!/bin/bash

set -e

echo "Clevo Fan Control Installer for KDE Plasma"
echo "==========================================="

# Проверка прав
if [ "$EUID" -eq 0 ]; then
    echo "Please don't run this script as root directly"
    echo "It will use sudo when needed"
    exit 1
fi

# Проверка наличия cmake
if ! command -v cmake &> /dev/null; then
    echo "CMake not found. Installing..."
    if command -v apt-get &> /dev/null; then
        sudo apt-get update
        sudo apt-get install -y cmake build-essential qtbase5-dev libkf5statusnotifieritem-dev
    elif command -v dnf &> /dev/null; then
        sudo dnf install -y cmake gcc-c++ qt5-qtbase-devel kf5-kstatusnotifieritem-devel
    elif command -v pacman &> /dev/null; then
        sudo pacman -S --noconfirm cmake base-devel qt5-base kstatusnotifieritem
    else
        echo "Unsupported distribution. Please install dependencies manually."
        exit 1
    fi
fi

# Сборка
echo "Building project..."
mkdir -p build
cd build
cmake ..
make

# Установка
echo "Installing (requires sudo)..."
sudo make install

echo "Done! You can now run 'clevo-indicator' from terminal or application menu."
