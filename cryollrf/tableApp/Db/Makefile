TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

#----------------------------------------------------
#  Optimization of db files using dbst (DEFAULT: NO)
#DB_OPT = YES

#----------------------------------------------------
# Create and install (or just install) into <top>/db
# databases, templates, substitutions like this
DB += tbl-read-longin.template
DB += tbl-read-bi.template
DB += tbl-read-ai.template
DB += tbl-read-stringin.template
DB += tbl-read-waveform.template

DB += tbl-write-longout.template
DB += tbl-write-bo.template
DB += tbl-write-ao.template
DB += tbl-write-waveform.template

#----------------------------------------------------
# If <anyname>.db template is not named <anyname>*.template add
# <anyname>_template = <templatename>

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

