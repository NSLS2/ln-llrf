#!../../bin/linux-x86_64/table

#- You may have to change table to something else
#- everywhere it appears in this file

< envPaths

cd "${TOP}"

## Register all support components
dbLoadDatabase "dbd/table.dbd"
table_registerRecordDeviceDriver pdbbase

## Load record instances

cd "${TOP}/iocBoot/${IOC}"
iocInit

## Start any sequence programs
