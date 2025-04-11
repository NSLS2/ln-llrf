#ifndef PARAMTABLEVALUEBASE_H
#define PARAMTABLEVALUEBASE_H

#include <string>
#include <list>
#include <map>
#include <typeinfo>
#include <stdexcept>
#include <ostream>

#include <tr1/memory>

#include <epicsMutex.h>
#include <epicsTime.h>

#include "table.h"

namespace paramTable {

//! Attempt to use an invalid value in an expression
class invalid_value_error : public std::runtime_error {
public:
    invalid_value_error(const std::string& s) : std::runtime_error(s) {}
};

//! Not writable
class access_error : public std::runtime_error {
public:
    access_error(const std::string& s) : std::runtime_error(s) {}
};

template<typename T>
class value;

template<typename T>
struct sample;

class table;

class group;

/** @brief Meta-data from a "sample"
 *
 * A sample is a copy of the value meta data.
 * In this case the severity and timestamp.
 *
 * This base class will not be instanciated directly, rather see sample .
 */
struct sampleBase {
    //! EPICS severity.  Typically 0 or INVALID_ALARM
    short severity;
    //! Timestamp when value was captured
    epicsTime timestamp;

    sampleBase() :severity(0), timestamp() {}
    sampleBase(short s, const epicsTime& t) :severity(s), timestamp(t) {}

    void showBase(std::ostream&) const;

protected:
    //! Use to implement operator= in sub-classes.
    void assign(const sampleBase& o) {
        severity=o.severity;
        timestamp=o.timestamp;
    }
};

/** @brief Access to the current value of a table parameter.
 *
 * Holds the current value meta-data, non-data
 * attributes, and the update subscriber (listener) list.
 */
class valueBase {
public:
    typedef callback_list<valueBase>  signal_t;
    typedef subscription_type connection_t;

protected:
    const std::string m_name;
    table * const m_table;

    short m_severity;
    epicsTime m_stamp;

    bool m_active; // aka. PACT
    bool m_changed;
    bool m_onchange;
    bool m_writeable;

    signal_t m_baseListeners;

    valueBase(const valueBase&);
    valueBase& operator=(const valueBase&);

    void init();
public:
    //! @brief Create parameter in table
    valueBase(table& t, const std::string& n);
    //! @brief Create parameter in group in table
    valueBase(group& t, const std::string& n);
    virtual ~valueBase();

    //! A reference to the owning table
    table& getTable(){return *m_table;}
    const table& getTable() const{return *m_table;}

    //! Full parameter name (DRV.TBL.PARAM)
    std::string fullName() const;
    const std::string& name() const{return m_name;}

    short severity() const{return m_severity;}
    //! Test for severity()!=INVALID_ALARM
    bool isValid() const;
    //! Throw an invalid_value_error if the severity is INVALID_ALARM
    void throwIfNotValid() const;
    void setSeverity(short);
    //! Mark current value as (in)valid
    void setValid(bool =true);

    //! Change Writable modifier.
    void setWritable(bool v){m_writeable=v;}
    bool writable() const{return m_writeable;}
    //! Throw an access_error unless writable()
    void throwIfNotWritable() const;

    /** Change notification behavour
     *
     @param v True - notify listeners only when value changes, False - notify whenever value is written
     */
    void setNotifyOnChange(bool v){m_onchange=v;}
    //! Find current notification behavour.  See setNotifyOnChange()
    bool notifyOnChange() const{return m_onchange;}

    //! Access current value timestamp
    const epicsTime& timestamp() const{return m_stamp;}
    //! Set new timestamp.  Must be later then previous timestamp, or ignored
    void setTimestamp(const epicsTime&);

    //! Access to type attributes for value type
    virtual const std::type_info& elementType() const=0;

    void markChanged();

    bool isChanged() const{return m_changed;}

    //! Connect a new un-typed listener
    connection_t connect(const signal_t::callback_type& s){return m_baseListeners.connect(s);}
    //! Remove the given subscription.  The listeners callback will never be invoked
    //! after this method runs.
    void disconnect(connection_t c){c->disconnect();}

    virtual void show(std::ostream&, int=0) const;

    //protected: // TODO: Needs to be accessed from table
    /** @brief Do not call directly.  Rather use table::dispatch()
     *
     * Responsible for invoking un-typed listeners
     */
    virtual void dispatch();
};

} // namespace paramTable

std::ostream& operator<<(std::ostream& strm, const paramTable::valueBase& s);

#endif // PARAMTABLEVALUEBASE_H
