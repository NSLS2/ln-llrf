#ifndef GROUP_H
#define GROUP_H

#include "valueBase.h"

#include <set>

#include <tr1/functional>

namespace paramTable {

class table;

/** @brief Adapter for several parameters
 *
 * An adapter to query or broadcast to a list of
 * @link paramTable::valueBase un-typed parameters @endlink .
 *
 * Presents an interface similar to paramTable::valueBase.
 */
class group {
    typedef std::set<valueBase*> m_members_t;
    m_members_t m_members;

    table *m_owner;
public:
    typedef valueBase::connection_t connection_t;
    typedef m_members_t::const_iterator const_iterator;

    group(table&);
    ~group();

    //! A reference to the owning table
    table& getTable() const{return *m_owner;}

    //! @brief Add parameter to group
    group& operator<<(valueBase& vb);

    //! @brief Remove parameter from group
    group& operator>>(valueBase& vb);

    size_t size() const{return m_members.size();}

    const_iterator begin() const{return m_members.begin();}
    const_iterator end() const{return m_members.end();}

    //! @brief Broadcast severity
    void setSeverity(short);
    //! @brief Broadcast INVALID severity
    void setValid(bool =true);

    //! @brief Comparison operations
    enum op {LT, LE, EQ, GE, GT, NE};

    //! @brief True if severities of all parameters match
    bool allSeverity(op, short) const;
    //! @brief True if severities of any parameter matches
    bool anySeverity(op, short) const;

    //! @brief True if severities of all parameters are <INVALID
    bool allValid() const;
    //! @brief True if severities of any parameter is <INVALID
    bool anyValid() const;


    /** @brief Broadcast notification behavour
     *
     @param v True - notify listeners only when value changes, False - notify whenever value is written
     */
    void setNotifyOnChange(bool v);

    bool allNotifyOnChange() const;
    bool anyNotifyOnChange() const;

    //! @brief Broadcast timestamp
    void setTimestamp(const epicsTime&);


    //! @brief Mark all parameters as changed.
    void markChanged();

    //! @brief True if all parameters are marked as changed
    bool allChanged() const;
    //! @brief True if any parameter is marked as changed
    bool anyChanged() const;


    //! @brief Connect a new un-typed listener to all parameters
    connection_t connect(const valueBase::signal_t::callback_type& s);

    //! @brief Remove the given subscription.
    //! The listeners callback will never be invoked after this method runs.
    void disconnect(connection_t c){c->disconnect();}
};

} // namespace paramTable

#endif // GROUP_H
