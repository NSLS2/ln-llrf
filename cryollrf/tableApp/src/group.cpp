
#include "paramtable/group.h"

#include <alarm.h>

#define FOREACHMEM(I) for(m_members_t::iterator I=m_members.begin(); I!=m_members.end(); ++I)

namespace paramTable {

group::group(table& o)
    :m_members()
    ,m_owner(&o)
{}

group::~group() {}

group& group::operator<<(valueBase& vb)
{
    m_members.insert(&vb);
    return *this;
}

group& group::operator>>(valueBase& vb)
{
    m_members.erase(&vb);
    return *this;
}

void group::setSeverity(short s)
{
    FOREACHMEM(m) {
        (*m)->setSeverity(s);
    }
}

void group::setValid(bool v)
{
    setSeverity(INVALID_ALARM);
}

static
bool compareSeverity(short a, group::op o, short b)
{
    switch(o) {
    case group::LT: return a< b;
    case group::LE: return a<=b;
    case group::EQ: return a==b;
    case group::GE: return a>=b;
    case group::GT: return a> b;
    case group::NE: return a!=b;
    default:
        return false;
    }
}

bool group::allSeverity(op o, short s) const
{
    FOREACHMEM(m) {
        short cur=(*m)->severity();
        if(!compareSeverity(cur,o,s))
            return false;
    }
    return true;
}
bool group::anySeverity(op o, short s) const
{
    FOREACHMEM(m) {
        short cur=(*m)->severity();
        if(compareSeverity(cur,o,s))
            return true;
    }
    return false;
}

bool group::allValid() const
{
    return allSeverity(LT,INVALID_ALARM);
}
bool group::anyValid() const
{
    return anySeverity(LT,INVALID_ALARM);
}


/** @brief Broadcast notification behavour
 *
 @param v True - notify listeners only when value changes, False - notify whenever value is written
 */
void group::setNotifyOnChange(bool v)
{
    FOREACHMEM(m) {
        (*m)->setNotifyOnChange(v);
    }
}

bool group::allNotifyOnChange() const
{
    FOREACHMEM(m) {
        if(!(*m)->notifyOnChange())
            return false;
    }
    return true;
}
bool group::anyNotifyOnChange() const
{
    FOREACHMEM(m) {
        if((*m)->notifyOnChange())
            return true;
    }
    return false;
}

//! @brief Broadcast timestamp
void group::setTimestamp(const epicsTime& ts)
{
    FOREACHMEM(m) {
        (*m)->setTimestamp(ts);
    }
}


void group::markChanged()
{
    FOREACHMEM(m) {
        (*m)->markChanged();
    }
}

bool group::allChanged() const
{
    FOREACHMEM(m) {
        if(!(*m)->isChanged())
            return false;
    }
    return true;
}
bool group::anyChanged() const
{
    FOREACHMEM(m) {
        if((*m)->isChanged())
            return true;
    }
    return false;
}

namespace {
    class group_subscription : public detail::subscription
    {
        typedef std::list<valueBase::connection_t> cons_t;
        cons_t cons;
    public:

        void add(valueBase&, const valueBase::signal_t::callback_type&);

        virtual void disconnect();

        virtual ~group_subscription();
    };

    void group_subscription::add(valueBase& vb, const valueBase::signal_t::callback_type& cb)
    {
        cons.push_back(vb.connect(cb));
    }

    void group_subscription::disconnect()
    {
        for(cons_t::iterator it=cons.begin(); it!=cons.end(); ++it) {
            (*it)->disconnect();
        }
        delete this;
    }

group_subscription::~group_subscription() {}
}
//! @brief Connect a new un-typed listener to all parameters
group::connection_t group::connect(const valueBase::signal_t::callback_type& s)
{
    std::auto_ptr<group_subscription> sub(new group_subscription);
    FOREACHMEM(m) {
        sub->add(*(*m), s);
    }
    return sub.release();
}

} // namespace paramTable
