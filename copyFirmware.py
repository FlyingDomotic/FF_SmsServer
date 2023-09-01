# This script copy firmware .bin and .elf files into 'Firmware' folder
#   Before, it asks user need to copy files from 'Firmware' folder to 'Firmware (previous)' one
import pathlib
import os
import shutil
import inspect
import time

thisScriptFullFileName = inspect.getouterframes(inspect.currentframe())[0].filename
thisScriptFileName = pathlib.Path(thisScriptFullFileName).name
thisScriptPath = pathlib.Path(thisScriptFullFileName).parent.resolve()

filesToCopy = ['*.bin', '*.elf']
firmwareFolder = os.path.join(thisScriptPath, 'Firmware')
firmwarePreviousFolder = os.path.join(thisScriptPath, 'Firmware (previous)')

# Do this only if Firmware folder exists
if os.path.isdir(firmwareFolder):
    # Ask fr user only if previous folder exists
    if os.path.isdir(firmwarePreviousFolder):
        fileCount = 0
        answer = input("Save current files into previous folder [No]? ")
        if answer == '' : answer = 'n'
        if answer[:1] in 'yYoO':
            for fileToCopy in filesToCopy:
                for file in pathlib.Path(firmwareFolder).rglob(fileToCopy):
                    shutil.copy(file, os.path.join(firmwareFolder, file.name))
                    fileCount += 1
            print(F"{pathlib.Path(__file__).name}: {fileCount} file{'s'[:fileCount^1]} moved")
    fileCount = 0
    PROJECT_BUILD_DIR = os.path.join(thisScriptPath, ".pio", "build")
    for fileToCopy in filesToCopy:
        for file in pathlib.Path(PROJECT_BUILD_DIR).rglob(fileToCopy):
            shutil.copy(file, os.path.join(firmwareFolder, file.name))
            fileCount += 1
    print(F"{thisScriptFileName}: {fileCount} file{'s'[:fileCount^1]} copied")
else:
    print(F"{thisScriptFileName}: folder {firmwareFolder} don't exists - Skipped")


time.sleep(5)
exit(0)