
#include "linkinfo.h"

#include <deque>
#include <sstream>

#include <ctype.h>

namespace paramTable {

driverEntry::driverEntry(const std::string& n, const kv_t& o)
    :m_name(n)
    ,m_options(o)
{}

class parse_error : public std::runtime_error
{
    static std::string at(size_t p, const std::string& msg)
    {
        std::ostringstream num;
        num<<"at char "<<p<<", "<<msg;
        return num.str();
    }

public:
    parse_error(size_t p, const std::string& msg)
        :std::runtime_error(at(p,msg))
    {}
};

namespace {

/* Grammar
 *
 * LNKSTR = DRVENT ( "|" DRVENT )*
 *
 * DRVENT = DRVNAME ( "," KEY "=" VALUE )*  (
 *
 * DRVNAME = KEY "." KEY "." KEY
 *
 * KEY = [:alphanum:]
 *
 * VALUE =  "\"" [^\"]* "\""
 */

struct pstate {
    enum type {
        drvname_key1,
        drvname_key2,
        drvname_key3,
        key,
        value,
        value_quoted
    };
};

} // namespase ""

driverConfig parseLink(const std::string& inp)
{
    pstate::type curstate = pstate::drvname_key1;
    driverConfig ret;
    std::string curdrv;
    driverEntry::kv_t curopts;
    std::string::size_type cur=0, len=inp.size();

    std::deque<std::string> stack;

    stack.push_back(std::string());

    while(cur!=std::string::npos && cur<len) {
        char c=inp[cur];

        switch(curstate) {
        case pstate::drvname_key1:
        case pstate::drvname_key2:
        case pstate::drvname_key3:

            if(isspace(c))
                break;

            else if(isalnum(c)) {
                stack.back()+=c;
                break;
            }

            if(c=='.' && curstate!=pstate::drvname_key3) {
                curstate=(pstate::type)(curstate+1);
                stack.push_back("");

            } else if(c==',') {
                curdrv=stack.pop_back();
                curdrv=stack.pop_back()+"."+curdrv;
                curdrv=stack.pop_back()+"."+curdrv;

                curstate=pstate::key;
                stack.push_back("");

            } else if(curstate==drvname_key3)
                throw parse_error(cur, "Expected '.'");

            break;

        case pstate::key:

            if(isspace(c))
                break;

            else if(isalnum(c)) {
                stack.back().buf+=c;
                break;
            }

            if(c!='=')
                throw parse_error(cur, "Expected '='");

            curstate=pstate::value;
            stack.push_back("");
            break;

        case pstate::value:

            if(isspace(c))
                break;

            else if(isalnum(c)) {
                stack.back().buf+=c;
                break;
            }

            if(c=='"'){
                curstate=pstate::value_quoted;
                break;

            }else if(c!=',' && c!='|')
                throw parse_error(cur, "Expected ',' or '|'");

            std::string value=stack.pop_back();
            curopts[stack.pop_back()]=value;

            if(c=='|') {
                ret.push_back(driverEntry(curdrv, curopts));
                curdrv.clear();
                curopts.clear();

                curstate=pstate::drvname_key1;

            } else
                curstate=pstate::key;

            break;

        case pstate::value_quoted:

            if(c=='"' && inp[cur-1]!='\\') {
                curstate=pstate::value;
                break;
            }
            stack.back().buf+=c;
            break;
        }

        cur++;

    }
}

} // namespace paramTable
