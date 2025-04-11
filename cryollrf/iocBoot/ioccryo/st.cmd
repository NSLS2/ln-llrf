#!../../bin/linux-x86_64/cryo

#- You may have to change cryo to something else
#- everywhere it appears in this file

< envPaths

cd "${TOP}"

## Register all support components
dbLoadDatabase "dbd/cryo.dbd"
cryo_registerRecordDeviceDriver pdbbase

system("export LD_LIBRARY_PATH=$(EPICS_BASE)/lib/linux-x86_64:$(TOP)/lib/linux-x86_64:$(LIBEVENT)/lib:$LD_LIBRARY_PATH")

createDRFM("PB", "10.0.138.16", 10, "500MHz")
#createDRFM("BUN", "10.0.138.8", 10, "3GHz")
#createDRFM("KLY1", "10.0.138.9", 10, "3GHz")
#createDRFM("KLY2", "10.0.138.10", 10, "3GHz")
#createDRFM("KLY3", "10.0.138.14", 10, "3GHz")
#createDRFM("KLY4", "10.0.138.15", 10, "3GHz")

## Load record instances
dbLoadRecords("db/drfm.db","P=LN-RF:PB{Cav},TBL=PB,MOSLO=14.06529064,MOOFF=-3.20221901,ADRVH=0.8,IDRVH=+1,IDRVL=-1,JDRVH=+1,JDRVL=-1")
#dbLoadRecords("db/drfm.db","P=LN-RF:BUN{Cav},TBL=BUN,MOSLO=17.23614711,MOOFF=-0.764491029,ADRVH=1.0,IDRVH=+1,IDRVL=-1,JDRVH=+1,JDRVL=-1")
#dbLoadRecords("db/drfm.db","P=LN-RF:1{Cav},TBL=KLY1,MOSLO=24.19470513,MOOFF=-0.238506707,ADRVH=0.8,IDRVH=+0.12,IDRVL=-0.12,JDRVH=+0.055,JDRVL=-0.055")
#dbLoadRecords("db/drfm.db","P=LN-RF:2{Cav},TBL=KLY2,MOSLO=16.05349874,MOOFF=-0.991046605,ADRVH=0.8,IDRVH=+0.12,IDRVL=-0.12,JDRVH=+0.055,JDRVL=-0.055")
#dbLoadRecords("db/drfm.db","P=LN-RF:3{Cav},TBL=KLY3,MOSLO=18.37684975,MOOFF=-0.895684102,ADRVH=0.8,IDRVH=+0.12,IDRVL=-0.12,JDRVH=+0.055,JDRVL=-0.055")
#dbLoadRecords("db/drfm.db","P=LN-RF:4{Cav},TBL=KLY4,MOSLO=18.37684975,MOOFF=-0.895684102,ADRVH=0.8,IDRVH=+0.12,IDRVL=-0.12,JDRVH=+0.055,JDRVL=-0.055")

dbLoadRecords("db/iocAdminSoft.db", "IOC=LN-CS{IOC:LLRF}")

asSetFilename("/cf-update/acf/default.acf")

dbLoadRecords("db/save_restoreStatus.db", "P=LN-CS{IOC:LLRF}")
save_restoreSet_status_prefix("LN-CS{IOC:LLRF}")

# ensure directories exist
system("install -d ${TOP}/as")
system("install -d ${TOP}/as/req")
system("install -d ${TOP}/as/save")

set_savefile_path("${TOP}/as","/save")
set_requestfile_path("${TOP}/as","/req")

set_pass0_restoreFile("rf_settings.sav")
set_pass1_restoreFile("rf_settings.sav")
set_pass0_restoreFile("rf_values.sav")
set_pass1_restoreFile("rf_values.sav")
set_pass1_restoreFile("rf_waveforms.sav")

cd "${TOP}/iocBoot/${IOC}"
callbackSetQueueSize(20000)
iocInit

makeAutosaveFileFromDbInfo("${TOP}/as/req/rf_settings.req", "autosaveFields_pass0")
makeAutosaveFileFromDbInfo("${TOP}/as/req/rf_values.req", "autosaveFields")
makeAutosaveFileFromDbInfo("${TOP}/as/req/rf_waveforms.req", "autosaveFields_pass1")

create_monitor_set("rf_settings.req", 5 , "")
create_monitor_set("rf_values.req", 5 , "")
create_monitor_set("rf_waveforms.req", 30 , "")

caPutLogInit("10.0.152.133:7004", 1)

dbl > records.dbl
system "cp records.dbl $CF_UPDATE_DIR/$HOSTNAME.$IOCNAME.dbl"

