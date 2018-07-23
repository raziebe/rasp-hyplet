#ccflags-y +=  -I. -DREQUIRES_VIRT=1

ccflags-y +=  -Iarch/arm64/truly/ -Iarch/arm/include/ -D__LINUX_ARM_ARCH__=8
obj-$(CONFIG_TRULY_PROTECT) += \
	hyp-init.o \
	truly_arm.o \
	mmu.o \
	trulyS.o \
	truly.o \
	tpdriver.o \
	ImageManager.o \
	osal_linux.o \
	common.o \
    	intervals.o \
	vector.o \
	memory_layout.o \
	tp_mmu.o \
	log_linux.o \
	linux_kernfile.o \
	ImageFile_linux.o \
	truly_user.o \
	Aes.o \
	elf.o
