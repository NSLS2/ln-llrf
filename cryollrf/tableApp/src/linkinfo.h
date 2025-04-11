#ifndef LINKINFO_H
#define LINKINFO_H

/** @file linkinfo.h
 * @brief Input/output link string parsing
 *
 * @section syntax Syntax
 *
 * eg. "driver.table.entry, key=value, ... | filtername, key=value, ..."
 */

#include <string>
#include <map>
#include <list>
#include <stdexcept>

namespace paramTable {


class driverEntry {
public:
    typedef std::map<std::string, std::string> kv_t;

private:
    const std::string m_name;

    const kv_t m_options;

public:
    driverEntry(const std::string& n, const kv_t& o);

    const std::string& name() const{return m_name;}

    const std::string& operator[](const std::string& k)
    {
        kv_t::const_iterator it=m_options.find(k);
        if(it==m_options.end())
            throw std::invalid_argument(std::string("Invalid key ")+k);
        return it->second;
    }
};

typedef std::list<driverEntry> driverConfig;

driverConfig parseLink(const std::string&);

}

#endif // LINKINFO_H
