

#include <stdlib.h>
#include <dbAccess.h>
#include <dbScan.h>
#include <link.h>
#include <epicsMath.h>
#include <devSup.h>
#include <recGbl.h>
#include <devLib.h> // For S_dev_*
#include <alarm.h>
#include <errlog.h>
#include <menuConvert.h>
#include <epicsTime.h>
#include <cvtTable.h>
#include <dbScan.h>
#include <menuFtype.h>

#include <sstream>
#include <stdexcept>
#include <string>
#include <deque>
#include <memory>
#include <vector>

#include <tr1/functional>

#include <waveformRecord.h>

#include "paramtable/scalar.h"

namespace {

using namespace paramTable;

using std::tr1::placeholders::_1;

struct devset {
    dset basic;
    DEVSUPFUN read;
};

struct devPrivBase {
    devPrivBase() :buffer_max(4) {scanIoInit(&notify);}

    table::shared_pointer ptable;

    IOSCANPVT notify;

    size_t buffer_max;

    epicsMutex devLock;
};

template<typename T>
struct devPriv : public devPrivBase {
    explicit devPriv(paramTable::value<T>* p) :devPrivBase(), param(p), subscribed(false) {}

    paramTable::value<T>* param;

    typedef typename paramTable::value<T>::sample_type sample_type;

    std::deque<sample_type> buffer;

    typename paramTable::value<T>::connection_t subscription;
    bool subscribed;
};

// Preserves the full type of ostringstream after operator<<
// Normally this returns std::ostream&
struct msgBuilder {
    std::ostringstream strm;
    operator std::string() const{return strm.str();}
};

template<typename T>
msgBuilder& operator<<(msgBuilder& b, T o){ b.strm<<o; return b;}


long init_record_empty(dbCommon*)
{
    return 0;
}

template<dsxt* D>
struct dev_init_gbl {
static inline
long init_gbl(int i)
{
    if(i==0)
        devExtend(D);
    return 0;
}
};

template<typename T>
void callback_val(devPriv<T>* priv, const paramTable::value<T>& update)
{
    Guard g(priv->devLock);

    if(priv->buffer.size()<priv->buffer_max)
        priv->buffer.push_back(update.snapshot());

    else {
        priv->buffer.back() = update.snapshot();
        return; // scan already requested
    }

    // Can't request a scan before the scan mechanism has been initialized
    if (interruptAccept)
        scanIoRequest(priv->notify);
}

long add_record(dbCommon* praw)
{
    waveformRecord* prec=(waveformRecord*)praw;
try{
    std::string lstr(prec->inp.value.instio.string);
    std::string pieces[2];

    {
        size_t dot=lstr.find_first_of('.');
        if(dot==std::string::npos) {
            msgBuilder build;
            throw std::runtime_error(build<<prec->name<<" : expected '.' in input link");
        }

        pieces[0]=lstr.substr(0, dot);

        size_t end=lstr.find_first_of(';', dot);

        pieces[1]=lstr.substr(dot+1, end);
    }

    table::shared_pointer t = table::getTable(pieces[0]);
    if(!t){
        msgBuilder build;
        throw std::runtime_error(build<<prec->name
                                 <<" : Invalid name: "<<pieces[0]);
    }

    Guard g(t->mutex());

    const std::type_info *info=0;
    switch(prec->ftvl) {
#define OP(TYPE,type) \
    case menuFtype ## TYPE: info = &typeid(type); break
    OP(CHAR,Int8Vector::value_type);
    OP(UCHAR,UInt8Vector::value_type);
    OP(SHORT,Int16Vector::value_type);
    OP(USHORT,UInt16Vector::value_type);
    OP(LONG,Int32Vector::value_type);
    OP(ULONG,UInt32Vector::value_type);
    OP(FLOAT,Float32Vector::value_type);
    OP(DOUBLE,Float64Vector::value_type);
#undef OP
    default:
        throw std::runtime_error("Unsupported FTVL");
    }

    valueBase *param = t->tryFindBase(pieces[1]);
    if(!param){
        t->add(pieces[1], *info);

        valueBase *param = t->tryFindBase(pieces[1]);
        if(!param){
            msgBuilder build;
            throw std::runtime_error(build<<prec->name
                                     <<" : Invalid parameter: "<<pieces[0]<<"."<<pieces[1]);
        }
    }

    if(param->elementType()!=*info) {
        msgBuilder msg;
        throw std::runtime_error(msg<<"Param '"<<pieces[0]<<"."<<pieces[1]<<"."<<pieces[2]<<" type does not match FTVL");
    }

    std::auto_ptr<devPrivBase> priv;

    switch(prec->ftvl) {
#define OP(TYPE,type) \
    case menuFtype ## TYPE: priv.reset(new devPriv<type>(dynamic_cast<paramTable::value<type>*>(param))); break
    OP(CHAR,Int8Vector::value_type);
    OP(UCHAR,UInt8Vector::value_type);
    OP(SHORT,Int16Vector::value_type);
    OP(USHORT,UInt16Vector::value_type);
    OP(LONG,Int32Vector::value_type);
    OP(ULONG,UInt32Vector::value_type);
    OP(FLOAT,Float32Vector::value_type);
    OP(DOUBLE,Float64Vector::value_type);
#undef OP
    default:
        throw std::runtime_error("Unsupported FTVL");
    }

    priv->ptable = t;

    prec->dpvt = (void*)priv.release();

    return 0;
}catch(std::exception& e){
    errlogPrintf("%s: add_record: %s\n", prec->name, e.what());
}
    (void)recGblSetSevr(prec, DISABLE_ALARM, INVALID_ALARM);
    return 0;
}

template<typename T>
struct dev_readwrite {
    typedef devPriv<T> priv_type;
    typedef T value_type;
    typedef T* pointer_type;

    static inline
    void subscribe(devPrivBase* bpriv, int cmd)
    {
        priv_type *priv=static_cast<priv_type*>(bpriv);

        if(cmd==0 && !priv->subscribed) {
            priv->subscription = priv->param->connect(std::tr1::bind(&callback_val<T>, priv, _1));
            priv->subscribed=true;
        } else if(priv->subscribed) {
            priv->param->disconnect(priv->subscription);
            priv->subscribed=false;
        }
    }

    static inline
    long read_val(waveformRecord* prec)
    {
        priv_type* priv=static_cast<priv_type*>((devPrivBase*)prec->dpvt);

        Guard g(priv->ptable->mutex());

        typename paramTable::value<T>::sample_type last;

        {
            Guard g(priv->devLock);

            if(priv->buffer.empty())
                last = priv->param->snapshot();

            else {
                last = priv->buffer.back();
                priv->buffer.pop_back();
            }
        }

        //Equivalent: prec->val = s.value;

        epicsUInt32 amount = last.value.size();
        if(amount>prec->nelm)
            amount=prec->nelm;

        std::copy(last.value.begin(),
                  last.value.begin()+amount,
                  static_cast<typename detail::primative<T>::type>(prec->bptr));

        prec->nord=amount;

        // capture and use return value to silence warning
        bool x=recGblSetSevr(prec, READ_ALARM, last.severity);
        if(prec->tse==epicsTimeEventDeviceTime && x==x) {
            prec->time = last.timestamp;
        }

        return 0;

    }

    static inline
    long write_val(waveformRecord* prec)
    {
        priv_type* priv=(priv_type*)prec->dpvt;

        Guard g(priv->ptable->mutex());

        paramTable::value<T>& p = *priv->param;

        typename priv_type::sample_type last(p.snapshot());

        p.get().clear(); // remove reference from value<>

        last.value.make_exclusive(); // In case it is still shared (eg. in buffer)

        last.value.resize(prec->nord);


        //Equivalent: s.value = prec->val;

        std::copy(static_cast<typename detail::primative<T>::type>(prec->bptr),
                  static_cast<typename detail::primative<T>::type>(prec->bptr)+prec->nord,
                  last.value.begin());

        last.severity=prec->nsev;

        priv->param->update(last);

        priv->ptable->dispatch();

        return 0;
    }

};

long del_record(dbCommon* praw)
{
    waveformRecord* prec=(waveformRecord*)praw;
    if(!praw->dpvt)
        return -1;
    try{
        std::auto_ptr<devPrivBase> priv((devPrivBase*)prec->dpvt);

        switch(prec->ftvl) {
    #define OP(TYPE,type) \
        case menuFtype ## TYPE: dev_readwrite<type>::subscribe(priv.get(), 1); break
        OP(CHAR,Int8Vector::value_type);
        OP(UCHAR,UInt8Vector::value_type);
        OP(SHORT,Int16Vector::value_type);
        OP(USHORT,UInt16Vector::value_type);
        OP(LONG,Int32Vector::value_type);
        OP(ULONG,UInt32Vector::value_type);
        OP(FLOAT,Float32Vector::value_type);
        OP(DOUBLE,Float64Vector::value_type);
    #undef OP
        default:
            throw std::runtime_error("Unsupported FTVL");
        }

        prec->dpvt=0;
        return 0;
    }catch(std::exception& e){
        errlogPrintf("%s: add_record: %s\n", prec->name, e.what());
    }
    (void)recGblSetSevr(prec, DISABLE_ALARM, INVALID_ALARM);
    return 0;
}

long get_iointr_info(int cmd, dbCommon *praw, IOSCANPVT *iopvt)
{
    waveformRecord* prec=(waveformRecord*)praw;
    if(!prec->dpvt)
        return -1;
    try{
        devPrivBase* priv=(devPrivBase*)prec->dpvt;
        *iopvt = priv->notify;

        switch(prec->ftvl) {
    #define OP(TYPE,type) \
        case menuFtype ## TYPE: dev_readwrite<type>::subscribe(priv, cmd); break
        OP(CHAR,Int8Vector::value_type);
        OP(UCHAR,UInt8Vector::value_type);
        OP(SHORT,Int16Vector::value_type);
        OP(USHORT,UInt16Vector::value_type);
        OP(LONG,Int32Vector::value_type);
        OP(ULONG,UInt32Vector::value_type);
        OP(FLOAT,Float32Vector::value_type);
        OP(DOUBLE,Float64Vector::value_type);
    #undef OP
        default:
            throw std::runtime_error("Unsupported FTVL");
        }

        return 0;
    }catch(std::exception& e){
        errlogPrintf("%s: add_record: %s\n", prec->name, e.what());
        return -1;
    }
}

long read_val(waveformRecord* prec)
{
    if(!prec->dpvt)
        return -1;
try{
    switch(prec->ftvl) {
#define OP(TYPE,type) case menuFtype ## TYPE: return dev_readwrite<type>::read_val(prec)
    OP(CHAR,Int8Vector::value_type);
    OP(UCHAR,UInt8Vector::value_type);
    OP(SHORT,Int16Vector::value_type);
    OP(USHORT,UInt16Vector::value_type);
    OP(LONG,Int32Vector::value_type);
    OP(ULONG,UInt32Vector::value_type);
    OP(FLOAT,Float32Vector::value_type);
    OP(DOUBLE,Float64Vector::value_type);
#undef OP
    }
}catch(invalid_value_error& e){
}catch(std::exception& e){
    errlogPrintf("%s: read_val: %s\n", prec->name, e.what());
}
    (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
    return 0;
}

long write_val(waveformRecord* prec)
{
    if(!prec->dpvt)
        return -1;
try{
    switch(prec->ftvl) {
#define OP(TYPE,type) case menuFtype ## TYPE: return dev_readwrite<type>::write_val(prec)
    OP(CHAR,Int8Vector::value_type);
    OP(UCHAR,UInt8Vector::value_type);
    OP(SHORT,Int16Vector::value_type);
    OP(USHORT,UInt16Vector::value_type);
    OP(LONG,Int32Vector::value_type);
    OP(ULONG,UInt32Vector::value_type);
    OP(FLOAT,Float32Vector::value_type);
    OP(DOUBLE,Float64Vector::value_type);
#undef OP
    }
}catch(invalid_value_error& e){
}catch(std::exception& e){
    errlogPrintf("%s: write_val: %s\n", prec->name, e.what());
}
    (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
    return 0;
}

dsxt dsxtdevTblWf = {&add_record, &del_record};

devset devTblWfIn = {
    {6,
     NULL,
     (DEVSUPFUN) &dev_init_gbl<&dsxtdevTblWf>::init_gbl,
     (DEVSUPFUN) &init_record_empty,
     (DEVSUPFUN) &get_iointr_info},
     (DEVSUPFUN) &read_val
};

devset devTblWfOut = {
    {6,
     NULL,
     (DEVSUPFUN) &dev_init_gbl<&dsxtdevTblWf>::init_gbl,
     (DEVSUPFUN) &init_record_empty,
     (DEVSUPFUN) &get_iointr_info},
     (DEVSUPFUN) &write_val
};

} // namespace ""

#include <epicsExport.h>

epicsExportAddress(dset,devTblWfIn);
epicsExportAddress(dset,devTblWfOut);
