'''
* Copyright (C) 2017 Texas Instruments Incorporated - http://www.ti.com/
* ALL RIGHTS RESERVED
'''

from tiovx import *

code = KernelExportCode("superset_module", Core.C66, "CUSTOM_APPLICATION_PATH")

kernel = Kernel("kernel_mem_allocation")

# image
kernel.setParameter(Type.IMAGE,   Direction.INPUT, ParamState.REQUIRED, "IN_IMAGE", ['VX_DF_IMAGE_U8'])
kernel.setParameter(Type.IMAGE,   Direction.OUTPUT, ParamState.OPTIONAL, "IN_IMAGE_OPT", ['VX_DF_IMAGE_U8'])

# this statement will fail due to array capacity not being an image attribute
kernel.allocateLocalMemory("img_scratch_mem", [Attribute.Array.CAPACITY], "IN_IMAGE")

kernel.setTarget(Target.DSP1)

code.export(kernel)
