#ifndef PARAMTABLE_H
#define PARAMTABLE_H

#include <map>
#include <list>

#include <tr1/memory>
#include <tr1/functional>

#include <epicsMutex.h>
#include <epicsGuard.h>

#include "cblist.h"

namespace paramTable {

typedef epicsGuard<epicsMutex> Guard;

class valueBase;
template<typename T>
class value;

/** @brief Container for a list of parameters.
 *
 * Holds several paramTable::value instances.  Keeps track of
 * which have been changed and handles notification
 * to registered listeners.
 *
 @warning Safe access to a table instance requires that the caller
 * own a shared_pointer pointing to this instance, and then
 * lock the table mutex(), before calling any other methods.
 */
class table : public std::tr1::enable_shared_from_this<table>
{
public:
    typedef std::tr1::shared_ptr<table> shared_pointer;

    typedef callback_list<valueBase>  signal_t;
    typedef subscription_type connection_t;

protected:
    const std::string m_instancename;

    void addParam(valueBase*, bool cleanup=false);
private:

    shared_pointer m_parent;
    const std::tr1::shared_ptr<epicsMutex> m_guard;

    typedef std::map<std::string, valueBase*> m_paramlookup_t;
    m_paramlookup_t m_paramlookup;

    std::list<valueBase*> m_cleanup;

    std::list<valueBase*> m_changelist;

    bool m_active;

    friend class valueBase;

    signal_t m_globalListeners;

    void markChanged(valueBase&);

    void init();

    table(const table&);
    table& operator=(const table&);
public:

    table(const std::string& instname);
    table(table& parent, const std::string& instname);
    virtual ~table();

    //! The mutex which must be taken before any other methods are called.
    epicsMutex& mutex() const{return *m_guard;}

    //! Register this table in the global list of tables
    void registerTable();
    //! Remove this table from the global list of tables
    void unregisterTable();

    void dispatch();

    const std::string& name() const{return m_instancename;}

    //! Add a un-typed listener which receive notification for all parameters
    connection_t connect(const signal_t::callback_type& cb ){return m_globalListeners.connect(cb);}
    void disconnect(connection_t c){c->disconnect();}

    /** Request that the given function or function object
     * be invoked for each parameter in this table.
     */
    template<typename C>
    void visitParams(C a) {
        for(m_paramlookup_t::const_iterator it=m_paramlookup.begin();
            it!=m_paramlookup.end();
            ++it)
        {
            a(it->second);
        }
    }

    virtual void add(const std::string& name, const std::type_info& t);

    //! @brief Un-typed Lookup parameter by name.
    //! @returns NULL if not found
    valueBase* tryFindBase(const std::string&) const;
    //! @brief Un-typed Lookup parameter by name
    //! @throws std::runtime_error if not found
    valueBase& findBase(const std::string&) const;

    //! @brief Typed Lookup parameter by name.
    //! @returns NULL if not found
    template<typename T>
    value<T>* tryFind(const std::string& n) const {
        valueBase* b=tryFindBase(n);
        if(!b)
            return 0;
        value<T>* t=dynamic_cast<value<T>* >(b);
        if(!t)
            return 0;
        return t;
    }

    //! @brief Typed Lookup parameter by name
    //! @throws std::runtime_error if not found
    template<typename T>
    value<T>& find(const std::string& n) const {
        valueBase* b=&findBase(n);
        value<T>* t=dynamic_cast<value<T>* >(b);
        if(!t)
            throw std::logic_error("Incorrect type");
        return *t;
    }

    /** Special notification called for all registered (see registerTable())
     * tables.
     * A good place to start worker threads.
     *
     * @ref tablelife
     */
    virtual void iocStart();
    /** Special notification called for all registered (see registerTable())
     * tables.
     * A good place to stop worker threads.
     *
     * @ref tablelife
     */
    virtual void iocStop();

    static void tableOnce();
    static void tableStart();
    static void tableStop();
    static bool tableRunning();
private:
    static void tableOnce(void*);

    struct tableSingle {
        typedef std::map<std::string, std::tr1::shared_ptr<table> > tables_t;
        tables_t tables;
        epicsMutex tablesGuard;
    };
    static tableSingle* tableSingleton;

public:
    //! Lookup table by name.
    static std::tr1::shared_ptr<table> getTable(const std::string& name);

    /** Request that the given function or function object
     * be invoked for each table in the global list.
     */
    static void visitTables(const std::tr1::function<void(const shared_pointer&)>& a);
    static void clearTables();
};

class flagGuard {
    bool *f;
public:
    flagGuard(bool& flag):f(&flag) {*f=true;}
    ~flagGuard(){*f=false;}
};

} // namespace paramTable

#endif // PARAMTABLE_H
