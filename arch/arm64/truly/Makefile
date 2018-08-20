ccflags-y +=  -Iarch/arm64/truly/ -Iarch/arm/include/ -D__LINUX_ARM_ARCH__=8
obj-$(CONFIG_HYPLET) += \
	hyplet_mmu.o \
	hypletS.o \
	hyplet.o \
	hyplet_gic.o \
	hyplet_rpc.o \
	hyp-init.o \
	truly_arm.o \
	mmu.o
