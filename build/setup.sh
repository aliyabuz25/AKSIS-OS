#!/bin/bash
# =============================================================================
# SHENLONG OS - Ortam Kurulum Scripti (macOS)
# =============================================================================

set -e

echo "============================================"
echo "  SHENLONG OS - Gelistirme Ortami Kurulumu"
echo "============================================"
echo ""

# Homebrew kontrolü
if ! command -v brew &>/dev/null; then
    echo "[!] Homebrew bulunamadi. Kuruluyor..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
fi

echo "[1] NASM kuruluyor..."
brew install nasm 2>/dev/null || echo "    nasm zaten kurulu"

echo "[2] QEMU kuruluyor..."
brew install qemu 2>/dev/null || echo "    qemu zaten kurulu"

echo "[3] i686-elf cross-compiler kontrol ediliyor..."
if ! command -v i686-elf-gcc &>/dev/null; then
    echo "    i686-elf-gcc bulunamadi. Derleniyor..."
    echo "    Bu islem ~20-30 dakika surebilir."
    echo ""

    BINUTILS_VER="2.41"
    GCC_VER="13.2.0"
    PREFIX="$HOME/.local/cross"
    TARGET="i686-elf"
    export PATH="$PREFIX/bin:$PATH"

    mkdir -p /tmp/cross-build
    cd /tmp/cross-build

    # Binutils
    if [ ! -f "binutils-${BINUTILS_VER}.tar.gz" ]; then
        echo "    binutils indiriliyor..."
        curl -LO "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VER}.tar.gz"
        tar xf "binutils-${BINUTILS_VER}.tar.gz"
    fi
    mkdir -p build-binutils && cd build-binutils
    ../binutils-${BINUTILS_VER}/configure \
        --target=$TARGET \
        --prefix=$PREFIX \
        --with-sysroot \
        --disable-nls \
        --disable-werror
    make -j$(sysctl -n hw.ncpu)
    make install
    cd ..

    # GCC
    if [ ! -f "gcc-${GCC_VER}.tar.gz" ]; then
        echo "    GCC indiriliyor..."
        curl -LO "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.gz"
        tar xf "gcc-${GCC_VER}.tar.gz"
        cd gcc-${GCC_VER} && ./contrib/download_prerequisites && cd ..
    fi
    mkdir -p build-gcc && cd build-gcc
    ../gcc-${GCC_VER}/configure \
        --target=$TARGET \
        --prefix=$PREFIX \
        --disable-nls \
        --enable-languages=c \
        --without-headers
    make -j$(sysctl -n hw.ncpu) all-gcc
    make -j$(sysctl -n hw.ncpu) all-target-libgcc
    make install-gcc
    make install-target-libgcc
    cd /tmp

    echo ""
    echo "    Cross-compiler kuruldu: $PREFIX/bin"
    echo "    PATH'e eklemek icin ~/.zshrc dosyaniza ekleyin:"
    echo "    export PATH=\"$PREFIX/bin:\$PATH\""
else
    echo "    i686-elf-gcc zaten kurulu: $(which i686-elf-gcc)"
fi

echo ""
echo "============================================"
echo "  Kurulum tamamlandi!"
echo "  Derlemek icin: cd $(dirname "$0")/.. && make"
echo "  Calistirmak icin: make run"
echo "============================================"