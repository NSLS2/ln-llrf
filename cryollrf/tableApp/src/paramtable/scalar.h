#ifndef PARAMTABLESCALAR_H
#define PARAMTABLESCALAR_H

#include <epicsTypes.h>

#include <tr1/functional>

#include "stridedata.h"
#include "valueBase.h"

namespace paramTable {

namespace detail {

// default scalar value used to initialize new containers
template<typename T>
struct defaultValue {
    static inline T get(){return 0;}
};

template<>
struct defaultValue<std::string> {
    static inline std::string get(){return std::string();}
};

template<typename T>
struct primative { typedef T type; };

template<typename E>
struct defMarkChanged {enum {value=1};};

// stride_data

template<typename E>
struct defaultValue<stride_data<E> > {
    typedef stride_data<E> vector_type;
    static inline vector_type get(){return vector_type();}
};

template<typename E>
struct primative<stride_data<E> > { typedef E* type; };


template<typename E>
struct defMarkChanged<stride_data<E> > {enum {value=0};};

} // namespace detail

/** @brief Value and meta-data container
 *
 * Meant to be efficiently copyable (aka. small)
 */
template<typename T>
struct sample : public sampleBase
{
    typedef T element_type;
    typedef T value_type;
    typedef T* pointer_type;

    //! The data
    T value;

    sample() :sampleBase(), value(detail::defaultValue<T>::get()) {}
    sample(T v, short s, const epicsTime& t) :sampleBase(s,t), value(v) {}
};

/** @brief Access to the current value of a table parameter.
 *
 * Holds the current value, associated meta-data, non-data
 * attributes, and the update subscriber (listener) list.
 *
 * @see @ref modparam
 */
template<typename T>
class value : public valueBase
{
public:
    typedef T value_type;
    typedef callback_list<value>  signal_t;
    typedef sample<T> sample_type;

private:
    value_type m_value;

    signal_t m_typedListeners;

    value(const value&);
    value& operator=(const value&);
public:
    //! Create a new parameter
    template<typename C>
    value(C& t, const std::string& n)
        :valueBase(t,n)
        ,m_value(detail::defaultValue<T>::get())
    {
        setNotifyOnChange(detail::defMarkChanged<T>::value);
    }
    //! Create a new parameter and register a member function of the table
    //! as a listener.
    template<typename C, typename FN>
    value(C& t, const std::string& n, FN fn)
        :valueBase(t,n)
        ,m_value(detail::defaultValue<T>::get())
    {
        setNotifyOnChange(detail::defMarkChanged<T>::value);
        connect(std::tr1::bind(fn, &t));
    }

    virtual ~value(){}

    //! Access to the type info for the value type.
    virtual const std::type_info& elementType() const{return typeid(T);}

    //! Value assignment.
    template<typename U>
    value& operator=(U v) {
        throwIfNotWritable();
        if(!notifyOnChange() || !isValid() || v!=m_value)
            markChanged();
        m_value=v;
        setValid();
        return *this;
    }

    template<typename U>
    bool operator==(U v) const {
        return v==m_value;
    }
    template<typename U>
    bool operator!=(U v) const {
        return !(*this==v);
    }

    /** Access to current data value.
     *
     * @throws invalid_value_error if the severity is INVALID_ALARM
     *
     * To facilitate transparent tracking of modifications, only constant references
     * are provided implicitly
     */
    operator const T&() const {
        throwIfNotValid();
        return m_value;
    }
    const T& operator->() const {
        throwIfNotValid();
        return m_value;
    }

    /** Access to current (possibly invalid) data
     *
     @warning It is the responsibility of the caller to ensure that any modifications
     * to the referenced object are safe, and that the value is marked as changed
     * with markChanged().
     */
    T& get() { return m_value; }
    const T& get() const { return m_value; }

    //! Create a snapshot of the current value, severity, and timestamp
    sample<T> snapshot() const{return sample<T>(m_value, m_severity, m_stamp);}

    //! Update using value, severity, and timestamp from the given sample
    void update(const sample<T>& v) {
        throwIfNotWritable();
        if(!notifyOnChange() || v.value!=m_value || v.severity!=m_severity || v.timestamp>m_stamp)
            markChanged();
        m_value = v.value;
        m_severity = v.severity;
        if(v.timestamp>m_stamp)
            m_stamp = v.timestamp;
    }

    //! Connect a typed listener
    connection_t connect(const typename signal_t::callback_type& cb ){return m_typedListeners.connect(cb);}

    virtual void show(std::ostream &strm, int lvl) const
    {
        valueBase::show(strm);
        strm<<m_value;
    }

    /** @brief Do not call directly.  Rather use table::dispatch()
     *
     * Responsible for invoking typed listeners
     */
    virtual void dispatch()
    {
        if(m_active || !isChanged())
            return;
        bool ro=!writable();
        if(!ro)
            setWritable(false);

        flagGuard g(m_active);
        m_typedListeners(*this);
        valueBase::dispatch();

        if(!ro)
            setWritable(true);
    }
};

/** @defgroup paramtypes Parameter Types
 *
 * The scalar and array parameter types
 * supported by the standard device supports.
 @{
 */

typedef value<epicsUInt32> UInt32; //!< An unsigned integer
typedef value<epicsInt32>  Int32; //!< An signed integer
typedef value<epicsFloat64> Float64; //!< An double
typedef value<std::string> String; //!< An variable length string


typedef value<stride_data<epicsUInt8> > UInt8Vector;
typedef value<stride_data<epicsInt8> > Int8Vector;

typedef value<stride_data<epicsUInt16> > UInt16Vector;
typedef value<stride_data<epicsInt16> > Int16Vector;

typedef value<stride_data<epicsUInt32> > UInt32Vector;
typedef value<stride_data<epicsInt32> > Int32Vector;

typedef value<stride_data<epicsFloat32> > Float32Vector;
typedef value<stride_data<epicsFloat64> > Float64Vector;

/** @} */

} // namespace paramTable

#endif // PARAMTABLESCALAR_H
