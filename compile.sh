export CROSS_COMPILE=/opt/gcc-linaro-4.9-2015.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
make  ARCH=arm64 -j4 LOCALVERSION=-TEE Image.gz
#make  ARCH=arm64 -j4 modules LOCALVERSION=-TEE
#make  vmlinux -o vmlinux.strip

