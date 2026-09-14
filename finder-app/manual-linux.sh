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

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

ROOTFS=${OUTDIR}/rootfs
#mkdir -p ${OUTDIR}

if [ ! -d ${OUTDIR} ]; then
	mkdir -p ${OUTDIR} || {
		echo "ERROR: Could not make directory ${OUTDIR}"
		exit 1
	}
fi

cd "$OUTDIR"

if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi

if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

   # kernel build steps here
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} Image dtbs
fi

echo "Adding the Image in outdir"
cp -r "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" "${OUTDIR}/"
echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# Create necessary base directories
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
    # using default for busybox
else
    cd busybox
fi

#  Make and install busybox
echo "busybox"
make distclean
make defconfig
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j4
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} CONFIG_PREFIX=${OUTDIR}/rootfs install
cd ${ROOTFS}

echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# Add library dependencies to rootfs

SYSROOT="$("${CROSS_COMPILE}gcc" -print-sysroot)"
#find the library dependencies regardless of the host machine
cp -L "$(find "${SYSROOT}" -name 'ld-linux-aarch64.so.1' -print -quit)" lib/
cp -L "$(find "${SYSROOT}" -name 'libc.so.6' -print -quit)" lib64/
cp -L "$(find "${SYSROOT}" -name 'libm.so.6' -print -quit)" lib64/
cp -L "$(find "${SYSROOT}" -name 'libresolv.so.2' -print -quit)" lib64/

# Make device nodes
mkdir -p dev
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 666 dev/console c 5 1
# Clean and build the writer utility

cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

cd ${ROOTFS}
# Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp -r ${FINDER_APP_DIR}/autorun-qemu.sh home
cp -r ${FINDER_APP_DIR}/Makefile home
cp -r ${FINDER_APP_DIR}/writer* home
cp -r ${FINDER_APP_DIR}/finder.sh home
cp -r ${FINDER_APP_DIR}/conf/assignment.txt home/conf
cp -r ${FINDER_APP_DIR}/conf/username.txt home/conf
cp -r ${FINDER_APP_DIR}/finder-test.sh home

# Chown the root directory
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
# Create initramfs.cpio.gz
gzip -f ${OUTDIR}/initramfs.cpio
