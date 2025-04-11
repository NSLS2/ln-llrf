#ifndef STRIDEDATA_H
#define STRIDEDATA_H

#include <ostream>
#include <algorithm>
#include <tr1/memory>

namespace paramTable {

template<typename E> class array_data;

/** @brief A holder for a contigious piece of memory.
 *
 * Stride data is shared, but offset and length are not.
 * This allows one stride to have access to only a
 * subset of a piece of memory.
 *
 * Also see @ref stridemem
 */
template<typename E>
class stride_data {
public:
    typedef E  element_type;
    typedef E* value_type;
    typedef E* iterator;
    typedef const E* const_iterator;
    typedef std::tr1::shared_ptr<E> shared_pointer_type;

    struct default_array_deleter {void operator()(E* a){delete[] a;}};

    friend class array_data<E>; // Only to modify m_count
private:
    shared_pointer_type m_data;
    //! Offset in the data array of first element
    size_t m_offset;
    //! Number of elements between m_offset and end of data
    size_t m_count;
public:

    //! @brief Empty stride (not very interesting)
    stride_data()
        :m_data(), m_offset(0), m_count(0)
    {}

    //! @brief Allocate (with new[]) a new stride of size c
    explicit stride_data(size_t c)
        :m_data(new E[c], default_array_deleter()), m_offset(0), m_count(c)
    {}

    //! @brief Allocate (with new[]) a new stride of size c and fill with value e
    stride_data(size_t c, element_type e)
        :m_data(new E[c], default_array_deleter()), m_offset(0), m_count(c)
    {
        std::fill_n(m_data.get(), m_count, e);
    }

    /** @brief Build stride from pointer
     *
     @param v A raw pointer allocated with new[].
     @param o The offset in v or the first element visible to the stride
     @param c The number of elements pointed to be v
     */
    stride_data(value_type v, size_t o, size_t c)
        :m_data(v, default_array_deleter()), m_offset(o), m_count(c)
    {}

    /** @brief Build stride from existing smart pointer
     *
     @param d An existing smart pointer
     @param o The offset in v or the first element visible to the stride
     @param c The number of elements pointed to be v
     */
    stride_data(const shared_pointer_type& d, size_t o, size_t c)
        :m_data(d), m_offset(o), m_count(c)
    {}

    /** @brief Build stride from raw pointer and cleanup
     *
     @param d An existing raw pointer
     @param b An function/functor used to free d.  Invoked as b(d).
     @param o The offset in v or the first element visible to the stride
     @param c The number of elements pointed to be v
     */
    template<typename A, typename B>
    stride_data(A d, B b, size_t o, size_t c)
        :m_data(d,b), m_offset(o), m_count(c)
    {}

    //! @brief Copy an existing stride
    stride_data(const stride_data& o)
        : m_data(o.m_data), m_offset(o.m_offset), m_count(o.m_count)
    {}
    //! @brief Copy an existing stride
    stride_data& operator=(const stride_data& o)
    {
        m_data=o.m_data;
        m_offset=o.m_offset;
        m_count=o.m_count;
        return *this;
    }

    //! @brief Swap the contents of this stride with another
    void swap(stride_data& o) {
        m_data.swap(o.m_data);
        std::swap(m_count, o.m_count);
        std::swap(m_offset, o.m_offset);
    }

    //! @brief Clear contents.
    //! size() becomes 0
    void clear() {
        m_data.reset();
        m_offset=0;
        m_count=0;
    }

    /** @brief Grow or shrink array
     *
     * A side effect is that array data is exclusively owned by this instance
     * as if make_exclusive() was called.  This holds even if the size does not change.
     */
    void resize(size_t i) {
        if(i==m_count) {
            make_exclusive();
            return;
        }
        value_type temp=new E[i];
        try{
            std::copy(begin(),
                      begin()+std::min(i,size()),
                      temp);
            m_data.reset(temp, default_array_deleter());
        }catch(...){
            delete[] temp;
        }
        m_offset=0;
        m_count=i;
    }

    /** @brief Grow (and fill) or shrink array.
     *
     * see @ref resize(size_t)
     */
    void resize(size_t i, E v) {
        size_t oldsize=size();
        resize(i);
        if(size()>oldsize) {
            size_t added=size()-oldsize;

            std::fill(end()-added, end(), v);
        }
    }

    bool valid() const{return m_data && size;}

    //! @brief Stride data is not shared
    bool is_exclusive() const {return !m_data || m_data.unique();}

    //! @brief Ensure (by copying) that this stride_data is the sole
    //! owner of the data array
    void make_exclusive() {
        if(!m_data || m_data.unique())
            return;
        shared_pointer_type d(new E[m_count], default_array_deleter());
        std::copy(m_data.get()+m_offset,
                  m_data.get()+m_offset+m_count,
                  d.get());
        m_data=d;
        m_offset=0;
    }

    //! @brief Number of elements visible through this stride
    size_t size() const{return m_count;}
    bool empty() const{return !m_count;}

    iterator begin(){return m_data.get()+m_offset;}
    const_iterator begin() const{return m_data.get()+m_offset;}

    iterator end(){return m_data.get()+m_offset+m_count;}
    const_iterator end() const{return m_data.get()+m_offset+m_count;}

    E& operator[](size_t i) {return m_data.get()[m_offset+i];}
    const E& operator[](size_t i) const {return m_data.get()[m_offset+i];}


    bool operator==(const stride_data& o) const {
        if(size()!=o.size())
            return false;
        return std::equal(begin(), end(), o.begin());
    }
    bool operator!=(const stride_data& o) const {
        return !((*this)==o);
    }
};


} // namespace paramTable


template<typename E>
std::ostream& operator<<(std::ostream& strm, const paramTable::stride_data<E>& arr)
{
    strm<<'{'<<arr.size()<<"}[";
    for(size_t i=0; i<arr.size(); i++) {
        if(i>10) {
            strm<<"...";
            break;
        }
        strm<<arr[i];
        if(i+1<arr.size())
            strm<<", ";
    }
    strm<<']';
    return strm;
}

#endif // STRIDEDATA_H
