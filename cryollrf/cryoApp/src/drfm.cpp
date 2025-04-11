
#include <iostream>
#include <vector>

#include <errno.h>
#include <string.h>

#include <epicsThread.h>
#include <epicsTypes.h>
#include <epicsEvent.h>
#include <epicsExit.h>
#include <errlog.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/thread.h>
#include <event2/dns.h>
#include <event2/buffer.h>

#include <paramtable/scalar.h>
#include <paramtable/table.h>
#include <paramtable/group.h>

#define PI (3.14159265359)

using namespace paramTable;

int cryoDebug = 1;

namespace {

class drfm : public table, public epicsThreadRunable {

    group fromDevice;

    // Scalar settings

    //bits
    UInt32 reset;
    bool cmd_reset;
    UInt32 trigedge;
    UInt32 affctrl;
    UInt32 reboot;
    bool cmd_reboot;
    UInt32 affreset;
    bool cmd_affreset;
    UInt32 writetables;
    bool cmd_writetables;

    UInt32 gain_amp;
    UInt32 gain_pha;

    UInt32 bwidth_amp;
    UInt32 bwidth_pha;

    UInt32 trig_delay;
    UInt32 time_run;

    UInt32 mo_high;
    UInt32 mo_low;

    UInt32 temp_warn;
    UInt32 temp_err;

    UInt32 stab_amp_h;
    UInt32 stab_amp_l;

    UInt32 probe_cal_pha;

    UInt32 dac_off_i;
    UInt32 dac_off_q;

    UInt32 stab_pha_h;
    UInt32 stab_pha_l;

    UInt32 aff_corr_lim;

    UInt32 fill_time;

    UInt32 stab_evnt_dly;
    UInt32 stab_evnt_max;

    UInt32 amp_thres;

    UInt32 loop_delay;

    // Vector settings

    Float64Vector ff_amp;
    Float64Vector ff_pha;
    Float64Vector sp_amp;
    Float64Vector sp_pha;

    // Scalar readbacks

    UInt32 comm_count;
    UInt32 err_sum;
    UInt32 ilock;

    UInt32 stab_amp_sts;
    UInt32 stab_pha_sts;
    UInt32 mo_sts;
    UInt32 mo_clk_sts;
    UInt32 temp_warn_sts;
    UInt32 temp_err_sts;

    UInt32 affctrl_sts;

    UInt32 mo_amp;
    UInt32 mo_pha;

    UInt32 temp;

    Float64 fw_loop_time;

    // Vector readbacks

    Float64Vector ff_amp_rb;
    Float64Vector ff_pha_rb;
    Float64Vector sp_amp_rb;
    Float64Vector sp_pha_rb;

    // Software

    UInt32 model;

    Float64 updatePeriod;

    UInt32 tryConnect;
    UInt32 connected;
    UInt32 rxcount;
    UInt32 txcount;

    Float64Vector timebase;

    String message;

    // internals

    UInt32 commit;

    epicsTime startUpdate, endUpdate;

    std::string host;
    unsigned short port;

    epicsUInt32 next_header;
    size_t expect;

    epicsThread runner;

    std::vector<epicsUInt32> scratch;

    struct event_base *reactor;
    struct event *reconnect_timo;
    bool reconnect_scheduled;
    struct evdns_base *resolver;
    struct bufferevent *session;

    bool scalarready;
    bool tableready;

    void changeConnect();

    void close_connection();

    void sendscalar();
    void sendtable();
    void settime();

    void recvscalar(const epicsUInt32*, size_t);
    void recvff(const epicsUInt32*, size_t);
    void recvsp(const epicsUInt32*, size_t);
    void senddata();

    virtual void run();

    template<bool (drfm::*V)>
    void action() {
        this->*V = true;
        scalarready = true;
    }
    template<bool (drfm::*V)>
    void markReady() {
        this->*V = true;
    }

public:
    drfm(const char*, const char*, unsigned short, const char*);

    // Callbacks for libevent
    // run from C code
    void eventcb(short evt);
    void recvdata();
    void stop();
    void start_connection();
};

extern "C" void drfm_event_cb(bufferevent*, short evts, void *priv)
{
    drfm *ctrl=(drfm*)priv;
    try {
        Guard g(ctrl->mutex());
        ctrl->eventcb(evts);
        ctrl->dispatch();
    }catch(std::exception& e){
        errlogPrintf("%s: Exception in drfm_event_cb: %s\n",
                     ctrl->name().c_str(), e.what());
    }
}

extern "C" void drfm_data_cb(bufferevent*, void *priv)
{
    drfm *ctrl=(drfm*)priv;
    try {
        Guard g(ctrl->mutex());
        ctrl->recvdata();
        ctrl->dispatch();
    }catch(std::exception& e){
        errlogPrintf("%s: Exception in drfm_data_cb: %s\n",
                     ctrl->name().c_str(), e.what());
    }
}

extern "C" void drfm_shutdown(void* priv)
{
    drfm *ctrl=(drfm*)priv;
    try {
        ctrl->stop();
    }catch(std::exception& e){
        errlogPrintf("%s: Exception in drfm_shutdown: %s\n",
                     ctrl->name().c_str(), e.what());
    }
}

extern "C" void drfm_reconnect(int,short,void* priv)
{
    drfm *ctrl=(drfm*)priv;
    try {
        Guard g(ctrl->mutex());
        ctrl->start_connection();
        ctrl->dispatch();
    }catch(std::exception& e){
        errlogPrintf("%s: Exception in drfm_reconnect: %s\n",
                     ctrl->name().c_str(), e.what());
    }
}

drfm::drfm(const char *name, const char* host, unsigned short port, const char* type)
    :table(name)
    ,epicsThreadRunable()
    ,fromDevice(*this)
    ,reset(*this,"Reset", &drfm::action<&drfm::cmd_reset>)
    ,cmd_reset(false)
    ,trigedge(*this,"Trig Edge", &drfm::markReady<&drfm::scalarready>)
    ,affctrl(*this,"AFF Loop", &drfm::markReady<&drfm::scalarready>)
    ,reboot(*this,"Reboot", &drfm::action<&drfm::cmd_reboot>)
    ,cmd_reboot(false)
    ,affreset(*this,"AFF Reset", &drfm::action<&drfm::cmd_affreset>)
    ,cmd_affreset(false)
    ,writetables(*this,"WriteTable", &drfm::action<&drfm::cmd_writetables>)
    ,cmd_writetables(false)

    ,gain_amp(*this,"Gain Amp", &drfm::markReady<&drfm::scalarready>)
    ,gain_pha(*this,"Gain Phase", &drfm::markReady<&drfm::scalarready>)

    ,bwidth_amp(*this,"Bandwidth Amp", &drfm::markReady<&drfm::scalarready>)
    ,bwidth_pha(*this,"Bandwidth Phase", &drfm::markReady<&drfm::scalarready>)

    ,trig_delay(*this,"Trig Delay", &drfm::markReady<&drfm::scalarready>)
    ,time_run(*this,"Pulse Time", &drfm::markReady<&drfm::scalarready>)

    ,mo_high(*this,"MO Amp High", &drfm::markReady<&drfm::scalarready>)
    ,mo_low(*this,"MO Amp Low", &drfm::markReady<&drfm::scalarready>)

    ,temp_warn(*this,"Temp Warn", &drfm::markReady<&drfm::scalarready>)
    ,temp_err(*this,"Temp Err", &drfm::markReady<&drfm::scalarready>)

    ,stab_amp_h(*this,"Stab Amp High", &drfm::markReady<&drfm::scalarready>)
    ,stab_amp_l(*this,"Stab Amp Low", &drfm::markReady<&drfm::scalarready>)

    ,probe_cal_pha(*this,"Probe Cal Phase", &drfm::markReady<&drfm::scalarready>)

    ,dac_off_i(*this,"DAC Offset I", &drfm::markReady<&drfm::scalarready>)
    ,dac_off_q(*this,"DAC Offset Q", &drfm::markReady<&drfm::scalarready>)

    ,stab_pha_h(*this,"Stab Phase High", &drfm::markReady<&drfm::scalarready>)
    ,stab_pha_l(*this,"Stab Phase Low", &drfm::markReady<&drfm::scalarready>)

    ,aff_corr_lim(*this,"AFF Corr Lim", &drfm::markReady<&drfm::scalarready>)

    ,fill_time(*this,"Fill Time", &drfm::markReady<&drfm::scalarready>)

    ,stab_evnt_dly(*this,"Stab Events Delay", &drfm::markReady<&drfm::scalarready>)
    ,stab_evnt_max(*this,"Stab Events Max", &drfm::markReady<&drfm::scalarready>)

    ,amp_thres(*this,"Amp Thres for Probe Cal", &drfm::markReady<&drfm::scalarready>)

    ,loop_delay(*this,"Loop Time", &drfm::markReady<&drfm::scalarready>)

// Vector settings

    ,ff_amp(*this,"FF Amp",&drfm::markReady<&drfm::tableready>)
    ,ff_pha(*this,"FF Phase", &drfm::markReady<&drfm::tableready>)
    ,sp_amp(*this,"SP Amp", &drfm::markReady<&drfm::tableready>)
    ,sp_pha(*this,"SP Phase", &drfm::markReady<&drfm::tableready>)

// Scalar readbacks

    ,comm_count(fromDevice,"Comm Count")
    ,err_sum(fromDevice,"Error Summery")
    ,ilock(fromDevice,"Interlock")

    ,stab_amp_sts(fromDevice,"Stab Amp Status")
    ,stab_pha_sts(fromDevice,"Stab Pha Status")
    ,mo_sts(fromDevice,"MO Status")
    ,mo_clk_sts(fromDevice,"MO Clock Status")
    ,temp_warn_sts(fromDevice,"Temp Warn Status")
    ,temp_err_sts(fromDevice,"Temp Err Status")

    ,affctrl_sts(fromDevice,"AFF Loop Status")

    ,mo_amp(fromDevice,"MO Amplitude")
    ,mo_pha(fromDevice,"MO Phase")

    ,temp(fromDevice,"Temp")

    ,fw_loop_time(fromDevice,"FW Loop Time")

// Vector readbacks

    ,ff_amp_rb(fromDevice,"FF Amp RB")
    ,ff_pha_rb(fromDevice,"FF Phase RB")
    ,sp_amp_rb(fromDevice,"SP Amp RB")
    ,sp_pha_rb(fromDevice,"SP Phase RB")

// Software
    ,model(*this,"Model")
    ,updatePeriod(fromDevice, "Update Period")
    ,tryConnect(*this,"Connect", &drfm::changeConnect)
    ,connected(*this,"Connected")
    ,rxcount(*this,"RX Count")
    ,txcount(*this,"TX Count")
    ,timebase(*this,"Time")
    ,message(*this,"Message")

// Internal
    ,commit(*this,"Commit", &drfm::senddata)
    ,startUpdate()
    ,endUpdate()
    ,host(host)
    ,port(port)
    ,next_header(0)
    ,expect(0)
    ,runner(*this, "drfm", epicsThreadGetStackSize(epicsThreadStackSmall), epicsThreadPriorityHigh)
    ,scratch(4001)
    ,reconnect_scheduled(false)
    ,session(0)
    ,scalarready(false)
    ,tableready(false)
{
    std::string stype(type);

    if(stype=="500MHz")
        model=0u;
    else if(stype=="3GHz")
        model=1u;
    else
        throw std::invalid_argument("Unknown module type ('500MHz', '3GHz'");

    model.setWritable(false);
    settime();

    if(cryoDebug)
        printf("Connecting to %s:%u\n", this->host.c_str(), this->port);

    tryConnect = 0u;
    connected = 0u;
    rxcount = 0u;
    txcount = 0u;

    commit.setNotifyOnChange(false);
    reset.setNotifyOnChange(false);
    reboot.setNotifyOnChange(false);
    affreset.setNotifyOnChange(false);
    writetables.setNotifyOnChange(false);

#if defined(WIN32)
    if(evthread_use_windows_threads()!=0)
#elif defined(EVENT__HAVE_PTHREADS)
    if(evthread_use_pthreads()!=0)
#else
#  warning libevent threading not enabled!!!
    if(false)
#endif
        throw std::runtime_error("libevent Threading failed to initialize");

    reactor = event_base_new();
    if(!reactor)
        throw std::bad_alloc();

    reconnect_timo = evtimer_new(reactor, &drfm_reconnect, (void*)this);
    if(!reconnect_timo)
        throw std::bad_alloc();

    resolver = evdns_base_new(reactor, 1);
    if(!resolver)
        throw std::bad_alloc();

    epicsAtExit(&drfm_shutdown, (void*)this);
    runner.start();
}

void drfm::start_connection()
{
    reconnect_scheduled=false;

    close_connection();

    if(cryoDebug)
        errlogPrintf("%s: Connect %s:%u\n", name().c_str(), host.c_str(), port);

    session = bufferevent_socket_new(reactor, -1,
                                     BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE|
                                     BEV_OPT_DEFER_CALLBACKS|BEV_OPT_UNLOCK_CALLBACKS);

    if(cryoDebug)
        errlogPrintf(" session=%8x\n", (unsigned long)session);

    if(!session) {
        throw std::bad_alloc();
    } else {
        }

    bufferevent_setcb(session, &drfm_data_cb, NULL, &drfm_event_cb, (void*)this);

    static const timeval timo={2,0};

    bufferevent_set_timeouts(session, &timo, NULL);

    // wait for a complete header
    expect=4;
    bufferevent_setwatermark(session, EV_READ, expect, 0);

    if(bufferevent_socket_connect_hostname(session, resolver, AF_UNSPEC,
                                           host.c_str(), port))
        throw std::runtime_error("Connection requested failed");

}

void drfm::close_connection()
{
    if(session) {
        bufferevent_free(session);
        session=0;
        next_header=0;
        expect=4;
        if(cryoDebug)
            errlogPrintf("%s: Disconnect\n", name().c_str());
        message = "Disconnect";
        connected=0u;
        fromDevice.setValid(false);
    }
}

void drfm::changeConnect()
{

    if(cryoDebug)
      errlogPrintf("changeConnect: connected=%d tryConnect=%d reconnect=%d\n", (int)connected, (int)tryConnect, (int)reconnect_scheduled );

    if(!connected && !tryConnect && reconnect_scheduled) {
        evtimer_del(reconnect_timo);
        reconnect_scheduled=false;
    }

    if(connected && !tryConnect) {
        close_connection();
      } else if(!connected && tryConnect && !reconnect_scheduled) {
        timeval timo = {1,0};
        evtimer_add(reconnect_timo, &timo);
        reconnect_scheduled=true;
        }

    if(!connected && !tryConnect && !reconnect_scheduled) {
        timeval timo = {1,0};
        evtimer_add(reconnect_timo, &timo);
        reconnect_scheduled=true;
        }
}

void drfm::sendscalar()
{
    if(!connected)
        return;
    try {
        if(cryoDebug) {
          errlogPrintf(" - sendscalar(): Trying...");
          if(cmd_reset) errlogPrintf("- RESET -");
          errlogPrintf("\n");
          }
        scratch[0]=htonl(0x10010000);

        epicsUInt32 bits=0;
        bits|=cmd_reset?(1<<0):0;
        bits|=trigedge?(1<<1):0;
        bits|=affctrl?(1<<2):0;
        bits|=cmd_reboot?(1<<3):0;
        bits|=cmd_affreset?(1<<4):0;
        bits|=cmd_writetables?(1<<5):0;

        cmd_reset=cmd_reboot=cmd_affreset=cmd_writetables=false;

        scratch[1]=htonl(bits);
        scratch[2]=htonl(gain_amp);
        scratch[3]=htonl(gain_pha);

        scratch[4]=0;
        scratch[5]=htonl(bwidth_amp);
        scratch[6]=htonl(bwidth_pha);
        scratch[7]=0;

        scratch[8]=htonl(trig_delay);
        scratch[9]=htonl(time_run);
        scratch[10]=htonl(mo_low);
        scratch[11]=htonl(mo_high);

        scratch[12]=htonl(temp_warn);
        scratch[13]=htonl(temp_err);
        scratch[14]=htonl(stab_amp_l);
        scratch[15]=htonl(stab_amp_h);

        scratch[16]=0;
        scratch[17]=htonl(probe_cal_pha);
        scratch[18]=htonl(dac_off_i);
        scratch[19]=htonl(dac_off_q);

        scratch[20]=htonl(stab_pha_l);
        scratch[21]=htonl(stab_pha_h);
        scratch[22]=htonl(aff_corr_lim);
        scratch[23]=htonl(fill_time);

        scratch[24]=htonl(stab_evnt_dly);
        scratch[25]=htonl(stab_evnt_max);
        scratch[26]=htonl(amp_thres);
        scratch[27]=htonl(loop_delay);

        scratch[28]=0;
        scratch[29]=0;
        scratch[30]=0;
        scratch[31]=0;

        if(cryoDebug)
          errlogPrintf(" - sendscalar(): Values set, RESET_CMD=%08x\n", bits);

    }catch(invalid_value_error& e){
        // don't send unless all inputs are valid
        message=std::string("Scalar set ")+e.what();
        if(cryoDebug)
          errlogPrintf("sendscalar(): Scalar set:%s\n", e.what() );
        return;
    }

    evbuffer *obuf=bufferevent_get_output(session);
    if(cryoDebug)
      errlogPrintf(" - sendscalar(): buflen=%d\n", evbuffer_get_length(obuf) );

    if(evbuffer_get_length(obuf)>2*4001*4) {
        scalarready=true;
        return;
    }

    if(cryoDebug) {
      errlogPrintf(" - sendscalar(): AFF_IN_D    = %08x\n", htonl(scratch[0]));
      errlogPrintf(" - sendscalar(): PCK_LEN_W   = %d\n",   htonl(scratch[1]));
      errlogPrintf(" - sendscalar(): RESET_CM    = %08x\n", htonl(scratch[2]));
      errlogPrintf(" - sendscalar(): GAIN_AMP_SP = %08x\n", htonl(scratch[3]));
      errlogPrintf(" - sendscalar(): GAIN_PH_SP  = %08x\n", htonl(scratch[4]));
      }

    if(bufferevent_write(session, &scratch[0], (model ? 32 : 28 )*4)!=0)
        throw std::runtime_error("Error sending scalar message");
    txcount = txcount + 1;

    if(cryoDebug)
      errlogPrintf(" - sendscalar(): %d sent-\n", (int)txcount);

}

void drfm::sendtable()
{
    if(!connected)
        return;
    try{
        scratch[0] = htonl(0x10020000);
        {
            const Float64Vector::value_type& wf=ff_amp;
            for(size_t i=0; i<1000; i++) {
                double v = i<wf.size() ? wf[i] : 0.0;
                v = std::max(0.0, std::min(v, 1.0));
                epicsUInt32 tmp=epicsUInt32(v*double(0x1ffff));
                scratch[i+1] = htonl(tmp);
            }
        }
        {
            const Float64Vector::value_type& wf=ff_pha;
            for(size_t i=0; i<1000; i++) {
                double v = i<wf.size() ? wf[i] : 0.0;
                v = std::max(-180.0, std::min(v, 180.0))/180.0;
                epicsInt32 tmp=epicsInt32(v*double(0x1ffff));
                scratch[i + 1001] = htonl(tmp);
            }
        }
        {
            const Float64Vector::value_type& wf=sp_amp;
            for(size_t i=0; i<1000; i++) {
                double v = i<wf.size() ? wf[i] : 0.0;
                v = std::max(0.0, std::min(v, 1.0));
                epicsUInt32 tmp=epicsUInt32(v*double(0x1ffff));
                scratch[i+2001] = htonl(tmp);
            }
        }
        {
            const Float64Vector::value_type& wf=sp_pha;
            for(size_t i=0; i<1000; i++) {
                double v = i<wf.size() ? wf[i] : 0.0;
                v = std::max(-180.0, std::min(v, 180.0))/180.0;
                epicsInt32 tmp=epicsInt32(v*double(0x1ffff));
                scratch[i + 3001] = htonl(tmp);
            }
        }

    }catch(invalid_value_error& e){
        // don't send unless all inputs are valid
        message=std::string("Table set ")+e.what();
        return;
    }

    evbuffer *obuf=bufferevent_get_output(session);
    if(evbuffer_get_length(obuf)>2*4001*4) {
        tableready=true;
        return;
    }

    if(bufferevent_write(session, &scratch[0], 4001*4)!=0)
        throw std::runtime_error("Error sending scalar message");
    txcount = txcount + 1;

}

void drfm::settime()
{
    //EGU: us
    double tinc=1.0;
    switch((int)model) {
    case 0: tinc=48.125e-3; break;
    case 1: tinc=37.383e-3; break;
    }

    Float64Vector::value_type& T(timebase.get());

    T.resize(1000);
    for(size_t i=0; i<T.size(); i++)
        T[i]=i*tinc;
    timebase.setValid(true);
    timebase.markChanged();
}

void drfm::recvscalar(const epicsUInt32 *data, size_t size)
{
    if(size<6)
        throw std::logic_error("scalar packet too small");

    startUpdate = epicsTime::getCurrent();

    comm_count = ntohl(data[0]);

    epicsUInt32 bits = ntohl(data[1]);

    err_sum       = (bits>>0)&1;
    ilock         = (bits>>1)&1;
    stab_amp_sts  = (bits>>2)&1;
    stab_pha_sts  = (bits>>3)&1;
    mo_sts        = (bits>>4)&1;
    mo_clk_sts    = (bits>>5)&1;
    temp_err_sts  = (bits>>6)&1;
    temp_warn_sts = (bits>>7)&1;
    affctrl_sts   = (bits>>8)&1;

    mo_amp = ntohl(data[2]);
    mo_pha = ntohl(data[3]);

    temp = ntohl(data[4]);

    //fw_loop_time = ntohl(data[5]);
}

void drfm::recvff(const epicsUInt32 *data, size_t size)
{
    if(size<2000)
        throw std::logic_error("ff packet too small");

    {
        Float64Vector::value_type& wf=ff_amp_rb.get();
        wf.resize(1000);
        for(size_t i=0; i<1000; i++) {
            wf[i] = ntohl(data[i])/double(0x1ffff);
        }
        ff_amp_rb.setValid(true);
        ff_amp_rb.markChanged();
    }
    {
        Float64Vector::value_type& wf=ff_pha_rb.get();
        wf.resize(1000);
        for(size_t i=0; i<1000; i++) {
            wf[i] = epicsInt32(ntohl(data[1000 + i]))/double(0x1ffff)*180.0;
        }
        ff_pha_rb.setValid(true);
        ff_pha_rb.markChanged();
    }
}

void drfm::recvsp(const epicsUInt32 *data, size_t size)
{
    if(size<2000)
        throw std::logic_error("sp packet too small");

    {
        Float64Vector::value_type& wf=sp_amp_rb.get();
        wf.resize(1000);
        for(size_t i=0; i<1000; i++) {
            wf[i] = ntohl(data[i])/double(0x1ffff);
        }
        sp_amp_rb.setValid(true);
        sp_amp_rb.markChanged();
    }
    {
        Float64Vector::value_type& wf=sp_pha_rb.get();
        wf.resize(1000);
        for(size_t i=0; i<1000; i++) {
            wf[i] = epicsInt32(ntohl(data[1000 + i]))/double(0x1ffff)*180.0;
        }
        sp_pha_rb.setValid(true);
        sp_pha_rb.markChanged();
    }

    epicsTime now(epicsTime::getCurrent());

    fw_loop_time=(now-startUpdate)*1000.0;
    updatePeriod=(now-endUpdate)*1000.0;

    endUpdate=now;
}

void drfm::eventcb(short evt)
{
    if(cryoDebug)
        errlogPrintf("eventcb(%x)\n", evt);
    if(evt&BEV_EVENT_CONNECTED) {
        connected = 1u;
        message = "Connected";
        if(cryoDebug)
            errlogPrintf("%s: Connected\n", name().c_str());

        changeConnect();
        if(!connected) {
            errlogPrintf("%s: abort\n", name().c_str());
            return;
        }

        bufferevent_enable(session, EV_WRITE|EV_READ);
        //bufferevent_set_timeouts(session,)

        // Device might have rebooted
        sendscalar();
        sendtable();
        if(cryoDebug)
            errlogPrintf("%s: Resynced\n", name().c_str());

    } else if(evt&(BEV_EVENT_ERROR|BEV_EVENT_EOF|BEV_EVENT_TIMEOUT)) {

        std::string msg;
        if(evt&BEV_EVENT_ERROR) {
            msg = "Socket Error: ";
            msg+=evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
        }else if(evt&BEV_EVENT_TIMEOUT) {
            msg = "Rx Timeout";
            close_connection();
        } else
            msg = "Connection Closed";

        if(cryoDebug)
            errlogPrintf("%s: %s\n", name().c_str(), msg.c_str());

        connected = 0u;
        if(tryConnect && !reconnect_scheduled) {
            timeval timo = {3,0};
            evtimer_add(reconnect_timo, &timo);
            reconnect_scheduled=true;
        }

        message = msg;

    } else {
        errlogPrintf("%s: eventcb %04x\n", name().c_str(), evt);
    }
}

void drfm::recvdata()
{
    struct evbuffer *buf = bufferevent_get_input(session);

    if(next_header==0) {
        if(evbuffer_get_length(buf)<4)
            return;

        evbuffer_remove(buf, (void*)&next_header, sizeof(next_header));

        next_header = ntohl(next_header);

        if((next_header&0xff000000) != 0x20000000) {
            errlogPrintf("%s: Invalid header %08x\n", name().c_str(), next_header);
            close_connection();
            message = "Protocol error";
            return;
        }

        switch(next_header) {
        case 0x20010000: expect=2000*4; break;
        case 0x20020000: expect=2000*4; break;
        case 0x20030000: expect=2000*4; break;
        case 0x20040000: expect=2000*4; break;
        default:
            close_connection();
            message = "Protocol error";
            errlogPrintf("%s: Unknown header %08x\n", name().c_str(), next_header);
            return;
        }
    }

    if(evbuffer_get_length(buf)>=expect) {
        epicsUInt32 *raw=(epicsUInt32*)evbuffer_pullup(buf, expect);

        rxcount = rxcount + 1;

        try {

            switch(next_header) {
            case 0x20010000: recvscalar(raw, expect); break;
            case 0x20020000: recvff(raw, expect); break;
            case 0x20030000: recvsp(raw, expect); break;
            case 0x20040000: break;
            }
        }catch(...){
            evbuffer_drain(buf, expect);

            expect=4;
            next_header=0;
            throw;
        }

        evbuffer_drain(buf, expect);

        expect=4;
        next_header=0;
    }

    bufferevent_setwatermark(session, EV_READ, expect, 0);
}

void drfm::senddata()
{
    if(scalarready) {
        scalarready=false;
        sendscalar();
    } else if(tableready) {
        tableready=false;
        sendtable();
    }
}

void drfm::run()
{
    Guard g(mutex());

    errlogPrintf("%s: Loop start\n", name().c_str());

    {
        epicsGuardRelease<epicsMutex> u(g);

        event_base_loop(reactor, 0);
    }

    errlogPrintf("%s: Loop done\n", name().c_str());
}

void drfm::stop()
{
    errlogPrintf("%s: Shutdown\n", name().c_str());
    {
        Guard g(mutex());
        if(session)
            bufferevent_free(session);
        session=0;
        if(reconnect_scheduled) {
            evtimer_del(reconnect_timo);
        }
        event_base_loopexit(reactor, NULL);
    }
    runner.exitWait();
}

} // namespace ""

extern "C"
void createDRFM(const char* name, const char* host, int port, const char* type)
{
    try {
        drfm::shared_pointer tbl(new drfm(name,host,port,type));
        tbl->registerTable();
    }catch(std::exception& e){
        std::cerr<<"Failed to create DRFM table: "<<name<<": "<<e.what()<<"\n";
    }
}

#include <iocsh.h>

static const iocshArg createDRFMArg0 = { "name",iocshArgString};
static const iocshArg createDRFMArg1 = { "host",iocshArgString};
static const iocshArg createDRFMArg2 = { "port",iocshArgInt};
static const iocshArg createDRFMArg3 = { "type",iocshArgString};
static const iocshArg * const createDRFMArgs[] = {&createDRFMArg0,&createDRFMArg1,&createDRFMArg2,&createDRFMArg3};
static const iocshFuncDef createDRFMFuncDef = {"createDRFM",4,createDRFMArgs};
static void createDRFMCallFunc(const iocshArgBuf *args)
{
    createDRFM(args[0].sval,args[1].sval,args[2].ival,args[3].sval);
}

static
void DRFMRegister(void)
{
    iocshRegister(&createDRFMFuncDef,createDRFMCallFunc);
}

#include <epicsExport.h>

epicsExportRegistrar(DRFMRegister);
epicsExportAddress(int,cryoDebug);
