

#include <stdlib.h>
#include <string.h>
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

#include <sstream>
#include <stdexcept>
#include <string>
#include <deque>
#include <memory>
#include <vector>

#include <tr1/functional>

#include <longoutRecord.h>
#include <longinRecord.h>
#include <boRecord.h>
#include <biRecord.h>
#include <mbboRecord.h>
#include <mbbiRecord.h>
#include <mbboDirectRecord.h>
#include <mbbiDirectRecord.h>
#include <aoRecord.h>
#include <aiRecord.h>
#include <stringoutRecord.h>
#include <stringinRecord.h>

#include "paramtable/scalar.h"

namespace {

using namespace paramTable;

using std::tr1::placeholders::_1;

struct devset {
    dset basic;
    DEVSUPFUN read;
};

#define DEVSUPPORT(NAME, RECTYPE, VALTYPE, INIT, IO) \
dsxt dsxt ## NAME = {&dev_adddel<VALTYPE,RECTYPE>::add_record, &dev_adddel<VALTYPE,RECTYPE>::del_record}; \
devset NAME = { \
    {6, \
     NULL, \
     (DEVSUPFUN) &dev_init_gbl<&dsxt ## NAME>::init_gbl, \
     (DEVSUPFUN) &init_record_ ## INIT, \
     (DEVSUPFUN) &dev_adddel<VALTYPE,RECTYPE>::get_iointr_info}, \
     (DEVSUPFUN) &dev_readwrite<VALTYPE,RECTYPE>:: IO ## _val \
}

template<typename T>
struct devPriv {
    devPriv() :buffer_max(4),subscribed(false) {}

    table::shared_pointer ptable;

    IOSCANPVT notify;

    paramTable::value<T>* param;

    typedef typename paramTable::value<T>::sample_type sample_type;

    std::deque<sample_type> buffer;
    size_t buffer_max;

    typename paramTable::value<T>::connection_t subscription;
    bool subscribed;

    epicsMutex devLock;
};

// Preserves the full type of ostringstream after operator<<
// Normally this returns std::ostream&
struct msgBuilder {
    std::ostringstream strm;
    operator std::string() const{return strm.str();}
};

template<typename T>
msgBuilder& operator<<(msgBuilder& b, T o){ b.strm<<o; return b;}

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

long init_record_empty(dbCommon*)
{
    return 0;
}

long init_record_return2(dbCommon*)
{
    return 2;
}

template<typename R>
struct linktype {enum {value=0};};

#define SELECT_REC_OUT(REC) \
template<> struct linktype<REC> {enum {value=1};}

template<typename R, int dir=linktype<R>::value>
struct dummy {
    static
    DBLINK* getlink(R* prec)
    {
        return &prec->inp;
    }
};

template<typename R>
struct dummy<R,1> {
    static
    DBLINK* getlink(R* prec)
    {
        return &prec->out;
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

template<typename T, typename R>
struct dev_adddel {

    static
    long add_record(dbCommon* praw)
    {
        R* prec=(R*)praw;
        try{
            DBLINK *plink=dummy<R>::getlink(prec);
            std::string lstr(plink->value.instio.string);

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

            std::auto_ptr<devPriv<T> > priv(new devPriv<T>);
            scanIoInit(&priv->notify);

            priv->ptable = table::getTable(pieces[0]);
            if(!priv->ptable){
                msgBuilder build;
                throw std::runtime_error(build<<prec->name
                                         <<" : Invalid name: "<<pieces[0]<<"."<<pieces[1]);
            }

            Guard g(priv->ptable->mutex());

            priv->param = priv->ptable->paramTable::table::tryFind<T>(pieces[1]);
            if(!priv->param){
                priv->ptable->add(pieces[1], typeid(T));

                priv->param = priv->ptable->paramTable::table::tryFind<T>(pieces[1]);

                if(!priv->param){
                    msgBuilder build;
                    throw std::runtime_error(build<<prec->name
                                             <<" : Invalid parameter: "<<pieces[0]<<"."<<pieces[1]);
                }

            }

            prec->dpvt = (void*)priv.release();

            //TODO: Update VAL???

            return 0;
        }catch(std::exception& e){
            errlogPrintf("%s: add_record: %s\n", prec->name, e.what());
        }
        (void)recGblSetSevr(prec, DISABLE_ALARM, INVALID_ALARM);
        return 0;
    }

    static
    long del_record(dbCommon* praw)
    {
        R* prec=(R*)praw;
        if(!praw->dpvt)
            return -1;
        try{
            std::auto_ptr<devPriv<T> > priv((devPriv<T>*)prec->dpvt);
            if(priv->subscribed)
                priv->param->disconnect(priv->subscription);
            priv->subscribed=false;
            prec->dpvt=0;
            return 0;
        }catch(std::exception& e){
            errlogPrintf("%s: del_record: %s\n", prec->name, e.what());
        }
        (void)recGblSetSevr(prec, DISABLE_ALARM, INVALID_ALARM);
        return 0;
    }

    static
    long get_iointr_info(int cmd, dbCommon *prec, IOSCANPVT *iopvt)
    {
        if(!prec->dpvt)
            return -1;
        try{
            devPriv<T>* priv=(devPriv<T>*)prec->dpvt;
            *iopvt = priv->notify;

            if(cmd==0 && !priv->subscribed) {
                priv->subscription = priv->param->connect(std::tr1::bind(&callback_val<T>, priv, _1));
                priv->subscribed=true;
            } else if(priv->subscribed) {
                priv->param->disconnect(priv->subscription);
                priv->subscribed=false;
            }

            return 0;
        }catch(std::exception& e){
            errlogPrintf("%s: add_record: %s\n", prec->name, e.what());
            return -1;
        }
    }
};

/* 0 - Assign VAL
 * 1 - Assign RVAL
 * 2 - Scale and assign to VAL
 * 3 - String copy
 */
template<typename T, typename R>
struct rec_ops_select {
    enum {value=0};
};

#define SELECT_REC_OP(T, R, N) \
template<> struct rec_ops_select<T,R> {enum {value=N};}

// ====== VAL ======

// Scalar
template<typename T, typename R, int S=rec_ops_select<T,R>::value>
struct rec_ops {
    static
    void assign(R* prec, const typename paramTable::value<T>::sample_type& s)
    {
        prec->val = s.value;
    }

    enum {assign_return=0};

    static
    void fetch(const R* prec, typename paramTable::value<T>::sample_type& s)
    {
        s.value = prec->val;
    }
};

// ====== RVAL ======

template<typename T, typename R>
struct rec_ops<T,R,1> {
    static
    void assign(R* prec, const typename paramTable::value<T>::sample_type& s)
    {
        prec->rval = s.value;
    }

    enum {assign_return=0};

    static
    void fetch(const R* prec, typename paramTable::value<T>::sample_type& s)
    {
        s.value = prec->rval;
    }
};

// ====== Scale VAL ======

template<typename T, typename R>
struct rec_ops<T,R,2> {
    static
    void assign(R* prec, const typename paramTable::value<T>::sample_type& s)
    {
        double value = s.value;

        // Perform same op as aoRecord
        switch (prec->linr) {
        case menuConvertNO_CONVERSION:
            break; /* do nothing*/
        case menuConvertLINEAR:
        case menuConvertSLOPE:
            if (prec->eslo == 0.0) value = 0;
            else value = (value - prec->eoff) / prec->eslo;
            break;
//        default:
//            if (cvtEngToRawBpt(&value, prec->linr, prec->init,
//                (void *)&prec->pbrk, &prec->lbrk) != 0) {
//                recGblSetSevr(prec, SOFT_ALARM, MAJOR_ALARM);
//                return;
//            }
        }
        value -= prec->aoff;
        if (prec->aslo != 0) value /= prec->aslo;
        prec->val = value;
        prec->udf = isnan(value);
    }

    enum {assign_return=2};

    static
    void fetch(const R* prec, typename paramTable::value<T>::sample_type& s)
    {
	double val = prec->val;
	/* adjust slope and offset */
	if(prec->aslo!=0.0) val*=prec->aslo;
	val+=prec->aoff;

	/* convert raw to engineering units and signal units */
	switch (prec->linr) {
	case menuConvertNO_CONVERSION:
		break; /* do nothing*/
	
	case menuConvertLINEAR:
	case menuConvertSLOPE:
		val = (val * prec->eslo) + prec->eoff;
		break;
	
//	default: /* must use breakpoint table */
//                if (cvtRawToEngBpt(&val,prec->linr,prec->init,(void *)&prec->pbrk,&prec->lbrk)!=0) {
//                      recGblSetSevr(prec,SOFT_ALARM,MAJOR_ALARM);
//                }
	}
        
        s.value = val;
    }
};

// ====== String VAL ======

template<typename T, typename R>
struct rec_ops<T,R,3> {
    static
    void assign(R* prec, const typename paramTable::value<T>::sample_type& s)
    {
        strncpy(prec->val, s.value.c_str(), sizeof(prec->val));
        prec->val[sizeof(prec->val)-1]='\0';
    }

    enum {assign_return=0};

    static
    void fetch(const R* prec, typename paramTable::value<T>::sample_type& s)
    {
        s.value = prec->val;
    }
};

template<typename T, typename R>
struct dev_readwrite {

    static inline
    long read_val(R* prec)
    {
        if(!prec->dpvt)
            return -1;
        try{
            devPriv<T>* priv=(devPriv<T>*)prec->dpvt;

            Guard g(priv->ptable->mutex());

            typename devPriv<T>::sample_type last;

            {
                Guard g(priv->devLock);

                if(priv->buffer.empty())
                    last = priv->param->snapshot();

                else {
                    last = priv->buffer.back();
                    priv->buffer.pop_back();
                }
            }

            rec_ops<T,R>::assign(prec, last);

            // capture and use return value to silence warning
            bool x=recGblSetSevr(prec, READ_ALARM, last.severity);
            if(prec->tse==epicsTimeEventDeviceTime && x==x) {
                prec->time = last.timestamp;
            }

            return rec_ops<T,R>::assign_return;
        }catch(invalid_value_error& e){
        }catch(std::exception& e){
            errlogPrintf("%s: read_val: %s\n", prec->name, e.what());
        }
        (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        return 0;
    }

    static inline
    long write_val(R* prec)
    {
        if(!prec->dpvt)
            return -1;
        try{
            devPriv<T>* priv=(devPriv<T>*)prec->dpvt;

            Guard g(priv->ptable->mutex());

            typename devPriv<T>::sample_type last;

            rec_ops<T,R>::fetch(prec, last);
            last.severity=prec->nsev;

            priv->param->update(last);

            priv->ptable->dispatch();

            return 0;
        }catch(invalid_value_error& e){
        }catch(std::exception& e){
            errlogPrintf("%s: write_val: %s\n", prec->name, e.what());
        }
        (void)recGblSetSevr(prec, WRITE_ALARM, INVALID_ALARM);
        return 0;
    }

};

/* Defaults
 * Link field is INP
 * Value field is VAL (OP=0)
 * Read returns 0
 */

// longout

SELECT_REC_OUT(longoutRecord);
DEVSUPPORT(devTblLOUInt32, longoutRecord, epicsUInt32, empty, write);
DEVSUPPORT(devTblLOInt32,  longoutRecord, epicsInt32,  empty, write);

// longin

DEVSUPPORT(devTblLIUInt32, longinRecord, epicsUInt32, empty, read);
DEVSUPPORT(devTblLIInt32,  longinRecord, epicsInt32,  empty, read);

// mbbo

SELECT_REC_OUT(mbboRecord);
SELECT_REC_OP(epicsUInt32, mbboRecord, 1);
DEVSUPPORT(devTblMBBOUInt32, mbboRecord, epicsUInt32, empty, write);
SELECT_REC_OP(epicsInt32, mbboRecord,  1);
DEVSUPPORT(devTblMBBOInt32,  mbboRecord, epicsInt32,  empty, write);

// mbbi

SELECT_REC_OP(epicsUInt32, mbbiRecord, 1);
DEVSUPPORT(devTblMBBIUInt32, mbbiRecord, epicsUInt32, empty, read);
SELECT_REC_OP(epicsInt32,  mbbiRecord, 1);
DEVSUPPORT(devTblMBBIInt32,  mbbiRecord, epicsInt32,  empty, read);

// mbboDirect

SELECT_REC_OUT(mbboDirectRecord);
SELECT_REC_OP(epicsUInt32, mbboDirectRecord, 1);
DEVSUPPORT(devTblMBBODirectUInt32, mbboDirectRecord, epicsUInt32, empty, write);
SELECT_REC_OP(epicsInt32,  mbboDirectRecord, 1);
DEVSUPPORT(devTblMBBODirectInt32,  mbboDirectRecord, epicsInt32,  empty, write);

// mbbiDirect

SELECT_REC_OP(epicsUInt32, mbbiDirectRecord, 1);
DEVSUPPORT(devTblMBBIDirectUInt32, mbbiDirectRecord, epicsUInt32, empty, read);
SELECT_REC_OP(epicsInt32,  mbbiDirectRecord, 1);
DEVSUPPORT(devTblMBBIDirectInt32,  mbbiDirectRecord, epicsInt32,  empty, read);

// bo

SELECT_REC_OUT(boRecord);
SELECT_REC_OP(epicsUInt32, boRecord, 1);
DEVSUPPORT(devTblBOUInt32, boRecord, epicsUInt32, empty, write);
SELECT_REC_OP(epicsInt32,  boRecord, 1);
DEVSUPPORT(devTblBOInt32,  boRecord, epicsInt32,  empty, write);

// bi

SELECT_REC_OP(epicsUInt32, biRecord, 1);
SELECT_REC_OP(epicsInt32,  biRecord, 1);
DEVSUPPORT(devTblBIUInt32, biRecord, epicsUInt32, empty, read);
DEVSUPPORT(devTblBIInt32,  biRecord, epicsInt32,  empty, read);

// ao

SELECT_REC_OUT(aoRecord);
SELECT_REC_OP(epicsUInt32, aoRecord, 1);
DEVSUPPORT(devTblAOUInt32, aoRecord, epicsUInt32, empty, write);
SELECT_REC_OP(epicsInt32,  aoRecord, 1);
DEVSUPPORT(devTblAOInt32,  aoRecord, epicsInt32,  empty, write);
SELECT_REC_OP(epicsFloat64, aoRecord, 2);
DEVSUPPORT(devTblAOFloat64, aoRecord, epicsFloat64, return2, write);

// ai

SELECT_REC_OP(epicsUInt32, aiRecord, 1);
DEVSUPPORT(devTblAIUInt32, aiRecord, epicsUInt32, empty, read);
SELECT_REC_OP(epicsInt32,  aiRecord, 1);
DEVSUPPORT(devTblAIInt32,  aiRecord, epicsInt32,  empty, read);
SELECT_REC_OP(epicsFloat64, aiRecord, 2);
DEVSUPPORT(devTblAIFloat64, aiRecord, epicsFloat64, return2, read);

// stringout

SELECT_REC_OUT(stringoutRecord);
SELECT_REC_OP(std::string, stringoutRecord, 3);
DEVSUPPORT(devTblSOString, stringoutRecord, std::string, empty, write);

// stringin

SELECT_REC_OP(std::string, stringinRecord, 3);
DEVSUPPORT(devTblSIString, stringinRecord, std::string, empty, read);


} // namespace ""

#include <epicsExport.h>

epicsExportAddress(dset,devTblLOUInt32);
epicsExportAddress(dset,devTblLOInt32);
epicsExportAddress(dset,devTblLIUInt32);
epicsExportAddress(dset,devTblLIInt32);

epicsExportAddress(dset,devTblMBBOUInt32);
epicsExportAddress(dset,devTblMBBOInt32);
epicsExportAddress(dset,devTblMBBIUInt32);
epicsExportAddress(dset,devTblMBBIInt32);

epicsExportAddress(dset,devTblMBBODirectUInt32);
epicsExportAddress(dset,devTblMBBODirectInt32);
epicsExportAddress(dset,devTblMBBIDirectUInt32);
epicsExportAddress(dset,devTblMBBIDirectInt32);

epicsExportAddress(dset,devTblBOUInt32);
epicsExportAddress(dset,devTblBOInt32);
epicsExportAddress(dset,devTblBIUInt32);
epicsExportAddress(dset,devTblBIInt32);

epicsExportAddress(dset,devTblAOUInt32);
epicsExportAddress(dset,devTblAOInt32);
epicsExportAddress(dset,devTblAOFloat64);
epicsExportAddress(dset,devTblAIUInt32);
epicsExportAddress(dset,devTblAIInt32);
epicsExportAddress(dset,devTblAIFloat64);

epicsExportAddress(dset,devTblSOString);
epicsExportAddress(dset,devTblSIString);
