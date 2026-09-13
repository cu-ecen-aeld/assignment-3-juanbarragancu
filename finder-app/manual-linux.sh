#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo, Juan Barragan

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
SYSROOT=/home/juba5219/arm-cross-compiler/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

ROOTFS=${OUTDIR}/rootfs
mkdir -p ${OUTDIR}

cd "$OUTDIR"

if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
echo "in git step"

if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
echo "mrproper"

    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
echo "defconfig"
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    echo "image modules dtbs"
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} Image dtbs
fi

echo "out git step"

echo "Adding the Image in outdir"
cp -r "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" "${OUTDIR}/"
echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir -p \
	"${ROOTFS}/bin" \
	"${ROOTFS}/dev" \
	"${ROOTFS}/etc" \
        "${ROOTFS}/home" \
	"${ROOTFS}/home/conf" \
	"${ROOTFS}/lib" \
	"${ROOTFS}/lib64" \
	"${ROOTFS}/proc" \
	"${ROOTFS}/sbin" \
        "${ROOTFS}/sys" \
	"${ROOTFS}/tmp" \
	"${ROOTFS}/usr" \
        "${ROOTFS}/var" \
	"${ROOTFS}/usr/bin" \
	"${ROOTFS}/usr/lib" \
	"${ROOTFS}/usr/sbin" \
       	"${ROOTFS}/var/log"

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
else
    cd busybox
fi

# TODO: Make and install busybox
echo "busybox"
make distclean
make defconfig
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j4
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} CONFIG_PREFIX=${OUTDIR}/rootfs install
cd ${ROOTFS}

echo "Library dependencies"
echo "first one"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
echo "second one"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
cp -L "${SYSROOT}lib/ld-linux-aarch64.so.1" lib
cp -L "${SYSROOT}lib64/libc.so.6" lib64
cp -L "${SYSROOT}lib64/libm.so.6" lib64
cp -L "${SYSROOT}lib64/libresolv.so.2" lib64
# TODO: Make device nodes
mkdir -p dev
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 666 dev/console c 5 1
# TODO: Clean and build the writer utility

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp -r /home/juba5219/assignment-1-juanbarragancu/finder-app/autorun-qemu.sh home
cp -r /home/juba5219/assignment-1-juanbarragancu/finder-app/Makefile home
cp -r /home/juba5219/assignment-1-juanbarragancu/finder-app/writer* home
cp -r /home/juba5219/assignment-1-juanbarragancu/finder-app/finder.sh home
cp -r /home/juba5219/assignment-1-juanbarragancu/finder-app/conf/assignment.txt home/conf
cp -r /home/juba5219/assignment-1-juanbarragancu/finder-app/conf/username.txt home/conf
cp -r /home/juba5219/assignment-1-juanbarragancu/finder-app/finder-test.sh home

# TODO: Chown the root directory
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
# TODO: Create initramfs.cpio.gz
gzip -f ${OUTDIR}/initramfs.cpio
