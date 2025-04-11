

#include <stdexcept>

#include <epicsStdio.h>
#include <epicsThread.h>

#include <errlog.h>

#include "paramtable/table.h"
#include "paramtable/valueBase.h"

static enum {iocStopped=0, iocRunning} iocState;

namespace paramTable {

/** Create a new root table.
 *
 * This table instance will have an independent lock
 */
table::table(const std::string& instname)
    :m_instancename(instname)
    ,m_parent()
    ,m_guard(new epicsMutex())
    ,m_paramlookup()
    ,m_cleanup()
    ,m_changelist()
    ,m_active(false)
    ,m_globalListeners()
{init();}

/** Create a dependent table
 *
 * This table will share the lock of another.
 * This can done to allow one table to be composed within another.
 */
table::table(table& parent, const std::string& instname)
    :m_instancename(instname)
    ,m_parent(parent.shared_from_this())
    ,m_guard(parent.m_guard)
    ,m_paramlookup()
    ,m_cleanup()
    ,m_changelist()
    ,m_active(false)
    ,m_globalListeners()
{init();}

void table::init()
{
    tableOnce();
    
    Guard g(tableSingleton->tablesGuard);

    // This test is redundent to registerTable() but is done anyway
    // to catch this before the sub-class ctor gets too far.
    if(tableSingleton->tables.find(m_instancename)!=tableSingleton->tables.end())
        throw std::runtime_error("Table name already in use");

}

table::~table()
{
    while(!m_cleanup.empty())
        delete *m_cleanup.begin();
}

/** @brief Transfer ownership of this valueBase
 *
 * Can be used by sub-classes to implement add()
 @param cleanup  True - Will be free'd with delete, False - cleanup is left to the caller
 */
void table::addParam(valueBase* param, bool cleanup)
{
    if(m_paramlookup.find(param->name())!=m_paramlookup.end()) {
        std::string m("Parameter name ");
        m += param->name();
        m += " already registered";
        throw std::logic_error(m);
    }
    m_paramlookup[param->name()]=param;
    if(cleanup)
        m_cleanup.push_back(param);
}

void table::registerTable()
{
    Guard g(tableSingleton->tablesGuard);

    if(tableSingleton->tables.find(m_instancename)!=tableSingleton->tables.end())
        throw std::runtime_error("Table name already registered");

    if(iocState==iocRunning)
        iocStart();

    tableSingleton->tables[m_instancename]=shared_from_this();
}

void table::unregisterTable()
{
    Guard g(tableSingleton->tablesGuard);

    tableSingleton->tables.erase(m_instancename);

    if(iocState==iocStopped)
        return; // already handled in exit hook

    iocStop();
}

/** @brief Run-time addition of parameters
 *
 * Sub-classes may implement this method to allow dynamic
 * addition of parameters.  See addParam().
 *
 * The default implementation always throws an invalid_argument exception.
 *
 @throws std::invalid_argument When a parameter can not be created
 *       because the name or type is invalid, or already exists.
 */
void table::add(const std::string& name, const std::type_info& t)
{
    std::string msg("Can not add ");
    msg+=name;
    msg+="(";
    msg+=t.name();
    msg+="): Dynamic parameter addition not supported";
    throw std::invalid_argument(msg);
}

valueBase* table::tryFindBase(const std::string& n) const
{
    m_paramlookup_t::const_iterator it=m_paramlookup.find(n);

    if(it==m_paramlookup.end())
        return 0;
    else
        return it->second;
}

valueBase& table::findBase(const std::string& n) const
{
    valueBase *p=tryFindBase(n);
    if(p)
        return *p;
    else
        throw std::runtime_error("Unknown parameter name");
}


void table::markChanged(valueBase& p)
{
    m_changelist.push_back(&p);
}

/** Consume list of changed parameters and invoke listener callbacks.
 *
 @warning This methods invokes callbacks which may change parameter values.
 *
 @note If invoked from within a callback it has no effect.
 */
void table::dispatch()
{
    if(m_active)
        return;
    flagGuard f(m_active);

    bool err=false;
    std::string firsterr;

    while(!m_changelist.empty()) {
        valueBase *cur = m_changelist.front();
        m_changelist.pop_front();

        try{
            cur->dispatch();
        }catch(std::exception& e){
            if(!err) {
                err=true;
                firsterr="Error during dispatch: ";
                firsterr+=e.what();
            }
        }

        // more parameters may have been changed by dispatch()

        // dispatch() clears m_changed
    }

    if(err)
        throw std::runtime_error(firsterr);
}

void table::iocStart()
{
}

void table::iocStop()
{
}

static epicsThreadOnceId tableId = EPICS_THREAD_ONCE_INIT;
table::tableSingle* table::tableSingleton = 0;

/** @brief Global initialization
 *
 * This function must be called at least once to initiallize
 * the global table list.
 * In EPICS IOCs it is automatically called during the driver
 * registeration phase.
 * When using parameter tables in other contexts it must be called explicitly.
 */
void table::tableOnce()
{
    char message[20] = "";
    epicsThreadOnce(&tableId, &tableOnce, message);
    if(message[0]!='\0')
        throw std::runtime_error(message);
}

static
void startTable(const paramTable::table::shared_pointer& tbl)
{
    paramTable::Guard g(tbl->mutex());
    try{
        tbl->iocStart();
    }catch(std::exception& e){
        errlogFlush();
        errlogPrintf("%s: Exception during iocStart: %s\n", tbl->name().c_str(), e.what());
        errlogFlush();
    }
}

void table::tableStart()
{
    tableOnce();
    Guard g(tableSingleton->tablesGuard);

    if(iocState==iocRunning)
        return;

    paramTable::table::visitTables(&startTable);
    iocState=iocRunning;
}

static
void stopTable(const paramTable::table::shared_pointer& tbl)
{
    try{
        paramTable::Guard g(tbl->mutex());
        tbl->iocStop();
    }catch(std::exception& e){
        errlogFlush();
        errlogPrintf("%s: Exception during iocStop: %s\n", tbl->name().c_str(), e.what());
        errlogFlush();
    }
}

void table::tableStop()
{
    tableOnce();
    Guard g(tableSingleton->tablesGuard);

    if(iocState!=iocRunning) {
        iocState=iocStopped;
        return;
    }
    iocState=iocStopped;

    paramTable::table::visitTables(&stopTable);
}

bool table::tableRunning()
{
    tableOnce();
    Guard g(tableSingleton->tablesGuard);
    return iocState==iocRunning;
}

void table::tableOnce(void* raw)
{
    char *message=static_cast<char*>(raw);

    try{
        tableSingleton = new tableSingle;
    }catch(std::exception& e){
        epicsSnprintf(message, 20, "%s", e.what());
        message[19]='\0';
    }
}

std::tr1::shared_ptr<table> table::getTable(const std::string& inst)
{
    tableOnce();
    Guard g(tableSingleton->tablesGuard);

    tableSingle::tables_t::const_iterator it=
            tableSingleton->tables.find(inst);
    if(it==tableSingleton->tables.end())
        return std::tr1::shared_ptr<table>();
    else
        return it->second;
}

void table::visitTables(const std::tr1::function<void(const shared_pointer&)>& a) {
    tableOnce();
    Guard g(tableSingleton->tablesGuard);
    for(tableSingle::tables_t::const_iterator it=tableSingleton->tables.begin();
        it!=tableSingleton->tables.end();
        ++it)
    {
        a(it->second);
    }
}

/** @brief  Remove all tables from the global table list.
 *
 * It is only save to call when the global state is not running.
 */
void table::clearTables()
{
    tableOnce();
    Guard g(tableSingleton->tablesGuard);

    tableSingleton->tables.clear();
}

namespace detail {
subscription::~subscription() {}
}

}
