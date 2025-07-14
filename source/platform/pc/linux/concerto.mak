

ifeq ($(TARGET_PLATFORM),PC)
ifeq ($(TARGET_OS),LINUX)

include $(PRELUDE)
TARGET      := vx_platform_pc
TARGETTYPE  := library

OS_FILES_REL_PATH = ../../os/posix
COMMON_FILES_REL_PATH = ../common

COMMON_FILES_BASE_PATH = $(TIOVX_PATH)/source/platform/pc/common


CSOURCES    := \
	$(OS_FILES_REL_PATH)/tivx_event.c \
	$(OS_FILES_REL_PATH)/tivx_mutex.c \
	$(OS_FILES_REL_PATH)/tivx_posix_objects.c  \
	$(OS_FILES_REL_PATH)/tivx_task.c  \
	$(OS_FILES_REL_PATH)/tivx_queue.c \
	$(COMMON_FILES_REL_PATH)/tivx_mem.c \
	$(COMMON_FILES_REL_PATH)/tivx_ipc.c \
	$(COMMON_FILES_REL_PATH)/tivx_init.c \
	$(COMMON_FILES_REL_PATH)/tivx_host.c \
	$(COMMON_FILES_REL_PATH)/tivx_target_config_pc.c \
	$(COMMON_FILES_REL_PATH)/tivx_platform_common.c \
	$(COMMON_FILES_REL_PATH)/vx_target_on_host_cpu.c \
    tivx_platform.c

ifeq ($(CONNECTOR_TP),IPPC_SHEM)
CSOURCES += $(OS_FILES_REL_PATH)/vx_producer_ippc.c $(OS_FILES_REL_PATH)/vx_consumer_ippc.c
DEFS += IPPC_SHEM_ENABLED
DEFS += BUILD_GC
endif

ifeq ($(CONNECTOR_TP),SOCKET)
CSOURCES += $(OS_FILES_REL_PATH)/vx_producer_sock.c $(OS_FILES_REL_PATH)/vx_consumer_sock.c
DEFS += SOCKET_ENABLED
DEFS += BUILD_GC
endif

DEFS        += LDRA_UNTESTABLE_CODE
# This is used to signify which sections of code is only applicable
# for the host for code coverage purposes. It has been left defined
# for all cores, but can be wrapped in the appropriate CPU when generating
# code coverage reports.
DEFS        += HOST_ONLY

IDIRS += $(TIOVX_PATH)/source/include $(COMMON_FILES_BASE_PATH) $(TIOVX_PATH)/utils/include $(IPPC_PATH)
IDIRS += $(APP_UTILS_PATH)
IDIRS += $(VISION_APPS_PATH)/platform/$(SOC)/rtos
IDIRS += $(TIOVX_PATH)/source/platform/os/posix

DEFS += _DISABLE_TIDL
IDIRS += $(CUSTOM_KERNEL_PATH)/include

include $(FINALE)

endif
endif
