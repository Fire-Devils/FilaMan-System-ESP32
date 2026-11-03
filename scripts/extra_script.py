from SCons.Script import COMMAND_LINE_TARGETS
import os

# PlatformIO uses a special Import function for the build environment object
Import("env")

# 1. If uploading the filesystem, ensure it gets built first automatically
if "uploadfs" in COMMAND_LINE_TARGETS:
    env.Depends("uploadfs", "buildfs")

# 2. If running a standard firmware upload, ensure the filesystem 
#    also builds and uploads automatically as a pre-requisite.
if "upload" in COMMAND_LINE_TARGETS:
    env.Depends("upload", "uploadfs")
    env.Depends("uploadfs", "buildfs")