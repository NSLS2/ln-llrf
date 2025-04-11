

#include <cstdio>
#include <stdexcept>
#include <sstream>

#include <drvSup.h>
#include <initHooks.h>
#include <epicsExit.h>
#include <epicsVersion.h>

#include "paramtable/table.h"
#include "paramtable/valueBase.h"

#ifndef VERSION_INT
#  define VERSION_INT(V,R,M,P) ( ((V)<<24) | ((R)<<16) | ((M)<<8) | (P))
#endif
#ifndef EPICS_VERSION_INT
#  define EPICS_VERSION_INT VERSION_INT(EPICS_VERSION, EPICS_REVISION, EPICS_MODIFICATION, EPICS_PATCH_LEVEL)
#endif

static void tableInfo(const paramTable::table::shared_pointer& t)
{
    // Check that the table has only one reference left (tableSingleton->table)
    if(!t.unique())
        epicsPrintf("%s has %lu dangling references\n", t->name().c_str(), t.use_count()-1);
}

extern "C" void paramTableShutdown(void*)
{
    paramTable::table::tableStop();
    paramTable::table::visitTables(&tableInfo);
    paramTable::table::clearTables();
}


extern "C" void paramTableInitHook(initHookState state)
{
#if EPICS_VERSION_INT < VERSION_INT(3,14,11,0)
    if(state!=initHookAtEnd)
        return;
    paramTable::table::tableStart();

#else /* >= 3.14.11 */

    switch(state) {
    case initHookAfterIocRunning:
        paramTable::table::tableStart();
        break;
    case initHookAtIocPause:
        paramTable::table::tableStop();
        break;
    default:
        break;
    }

#endif /* >= 3.14.11 */
}


namespace paramTable {
namespace {

void nameTable(const std::tr1::shared_ptr<table>& t)
{
    errlogPrintf("%s\n", t->name().c_str());
}

struct dbiortable {
    size_t i;
    int lvl;
    dbiortable() :i(0) {}
    void operator()(const std::tr1::shared_ptr<table>& t)
    {
        i++;
        if(lvl<=0)
            return;
        printf("%s\n", t->name().c_str());
    }
};

void showParam(valueBase* p)
{
    std::ostringstream strm;
    {
        Guard g(p->getTable().mutex());
        p->show(strm);
    }
    strm<<"\n";
    errlogMessage(strm.str().c_str());
}

} // namespace ""

} // namespace paramTable

extern "C"
void lstbl()
{
try{
    paramTable::table::visitTables(paramTable::nameTable);
}catch(std::exception& e){
    errlogPrintf("lstbl: %s\n", e.what());
    errlogFlush();
}
}

extern "C"
void showtbl(const char* n)
{
try{
    using paramTable::table;
    table::shared_pointer tbl = table::getTable(n);
    if(!tbl) {
        errlogPrintf("No such name: '%s'\n", n);
        return;
    }
    tbl->visitParams(paramTable::showParam);
}catch(std::exception& e){
    errlogPrintf("showtbl: %s\n", e.what());
}
}

static
void showTable(int lvl)
{
try{
    printf("Param Table Driver\n");
    paramTable::dbiortable info;
    info.lvl=lvl;
    paramTable::table::visitTables(std::tr1::ref(info));
    if(lvl==0) {
        printf(" %d table(s)\n", info.i);
    }
}catch(std::exception& e){
    errlogPrintf("showTable: %s\n", e.what());
}
}

#include <iocsh.h>

static const iocshArg * const lstblArgs[] = {};
static const iocshFuncDef lstblFuncDef = {"lstbl",0,lstblArgs};
static void lstblCallFunc(const iocshArgBuf *args)
{
    lstbl();
    errlogFlush();
}

static const iocshArg showtblArg0 = { "name",iocshArgString};
static const iocshArg * const showtblArgs[] = {&showtblArg0};
static const iocshFuncDef showtblFuncDef = {"showtbl",1,showtblArgs};
static void showtblCallFunc(const iocshArgBuf *args)
{
    showtbl(args[0].sval);
    errlogFlush();
}

static
void paramtableRegister(void)
{
    paramTable::table::tableOnce();
    iocshRegister(&lstblFuncDef,lstblCallFunc);
    iocshRegister(&showtblFuncDef,showtblCallFunc);
    initHookRegister(&paramTableInitHook);
    epicsAtExit(&paramTableShutdown,0);
}

static drvet drvparamtable = {
    2,
    (DRVSUPFUN) &showTable,
    NULL
};

#include <epicsExport.h>

epicsExportRegistrar(paramtableRegister);
epicsExportAddress(drvet,drvparamtable);
