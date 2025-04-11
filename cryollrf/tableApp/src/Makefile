TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

INC += paramtable/scalar.h
INC += paramtable/table.h
INC += paramtable/valueBase.h
INC += paramtable/group.h
INC += paramtable/cblist.h
INC += paramtable/arraydata.h
INC += paramtable/strideiter.h
INC += paramtable/stridedata.h

LIBRARY_IOC += paramtable

paramtable_SRCS += valueBase.cpp
paramtable_SRCS += group.cpp
paramtable_SRCS += table.cpp
paramtable_SRCS += tableioc.cpp
paramtable_SRCS += devSupport.cpp
paramtable_SRCS += devSupportWf.cpp

paramtable_LIBS += Com dbCore dbRecStd

DBD += paramtable.dbd

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

