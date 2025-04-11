
#include <sstream>

#include <alarm.h>

#include "paramtable/valueBase.h"
#include "paramtable/group.h"
#include "paramtable/table.h"
#include "paramtable/scalar.h"


namespace paramTable {

valueBase::valueBase(table& t, const std::string& n)
    :m_name(n)
    ,m_table(&t)
    ,m_severity(INVALID_ALARM)
    ,m_stamp()
    ,m_active(false)
    ,m_changed(false)
    ,m_onchange(true)
    ,m_writeable(true)
    ,m_baseListeners()
{init();}

valueBase::valueBase(group& g, const std::string& n)
    :m_name(n)
    ,m_table(&g.getTable())
    ,m_severity(INVALID_ALARM)
    ,m_stamp()
    ,m_active(false)
    ,m_changed(false)
    ,m_onchange(true)
    ,m_writeable(true)
    ,m_baseListeners()
{init(); g<<*this;}

void valueBase::init()
{
    m_table->addParam(this);
}

valueBase::~valueBase()
{
}

std::string valueBase::fullName() const
{
    return m_table->name()+"."+m_name;
}

bool valueBase::isValid() const
{
    return m_severity!=INVALID_ALARM;
}

void valueBase::throwIfNotValid() const
{
    if(!isValid()) {
        std::ostringstream msg;
        msg<<name()<<"."<<m_name<<" is not valid";
        throw invalid_value_error(msg.str());
    }
}

void valueBase::setSeverity(short v)
{
    if(v==m_severity)
        return;
    m_severity=v;
    markChanged();
}

void valueBase::setValid(bool v)
{
    setSeverity(v ? NO_ALARM : INVALID_ALARM);
}

void valueBase::throwIfNotWritable() const
{
    if(!writable()) {
        std::string msg(name());
        msg+=" is not writable";
        throw access_error(msg);
    }
}

void valueBase::setTimestamp(const epicsTime& ts)
{
    if(ts<=m_stamp)
        return;
    m_stamp=ts;
    markChanged();
}

void valueBase::markChanged()
{
    if(m_changed)
        return;
    m_table->markChanged(*this);
    m_changed=true;
}

void valueBase::dispatch()
{
    try{
        m_baseListeners(*this);
    }catch(...){
        // m_table.run(*this);
        m_changed = false;
        throw;
    }
    // m_table.run(*this);
    m_changed = false;
}

void valueBase::show(std::ostream& strm, int lvl) const
{
    strm<<name();

    switch(m_severity) {
    case MAJOR_ALARM:   strm<<"\t= Major: "; break;
    case MINOR_ALARM:   strm<<"\t= Minor: "; break;
    case INVALID_ALARM: strm<<"\t= Invalid: "; break;
    case NO_ALARM:      strm<<"\t= "; break;
    default:            strm<<"\t= Unnamed: ";
    }
}


} // namespace paramTable

std::ostream& operator<<(std::ostream& strm, const paramTable::valueBase& s)
{
    s.show(strm);
    return strm;
}
