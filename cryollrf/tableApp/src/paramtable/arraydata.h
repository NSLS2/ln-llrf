#ifndef ARRAYDATA_H
#define ARRAYDATA_H

#include <vector>
#include <algorithm>
#include <tr1/memory>

#include "stridedata.h"
#include "strideiter.h"

namespace paramTable {

/** @brief STL-like General purpose non-contigious array container.
 *
 * Implements (where possible) the interface of std::vector<E>.
 * Differs in that data may not be stored in one contigious memory
 * range.  This enables handling of very large arrays at the expense of
 * less efficient operations.
 *
 @warning Modifying the contents of and array_data requires special operations.  See below.
 *
 * Each array is composed of zero or more @link paramTable::stride_data strides @endlink.
 * A stride is a contigous piece of memory with a given length.  Each stride also holds
 * a destructor allowing it to be free'd in a user defined way.
 *
 * Strides can be appended to extend the array without a copy.
 *
 * Because the contents of an array_data instance may be shared care must be taken
 * to ensure not to modify shared data.  To do this transparently would require
 * that each operation check that the stride(s) it effects are not shared.
 * This would introduce significant overhead to element operations.
 *
 * So this check is not done and @b it @b is @b the @b responsibility @b of @b the @b user
 * @b to @b ensure @b that @b shared @b data @b is @b not @b modified.
 * This can be done using the make_exclusive() member
 * functions.
 *
 * Also be aware that most random access operations have O(N) runtime.
 * Where N is the number of strides involved.
 */
template<typename E>
class array_data
{
    struct _data {
        typedef std::vector<stride_data<E> > strides_t;
        strides_t strides;

        size_t tot_size;

        _data(): strides(), tot_size(0) {}
        _data(const _data& o): strides(o.strides), tot_size(o.tot_size) {}
        _data& operator=(const _data& o); // not implemented
    };
    typedef std::tr1::shared_ptr<_data> _data_pointer;
    _data_pointer data;

    void make_data_unique() {
        if(data.unique())
            return;
        data.reset(new _data(*data));
    }

public:
    typedef stride_data<E> stride;
    typedef typename _data::strides_t::iterator stride_iterator;
    typedef typename _data::strides_t::const_iterator const_stride_iterator;

    typedef array_iterator<E,stride_iterator> iterator;
    typedef array_iterator<const E,const_stride_iterator> const_iterator;

    //! Create a new empty array
    array_data(): data(new _data) {}

    //! Copy an existing array
    array_data(const array_data& o) :data(o.data) {}

    //! Copy an existing array
    array_data& operator =(const array_data& o) {
        data=o.data;
    }

    //! Clear contents (releases all references)
    void clear() {
        data.reset(new _data);
    }

    //! Equivalent to size()==0
    bool empty() const{return !data->tot_size;}
    //! The total number of elements in all strides
    size_t size() const{return data->tot_size;}
    //! The maximum number of elements (in all strides) that can be stored
    size_t max_size() const{return (size_t)-1;}

    /** @brief Reduce or extend the array size.
     *
     * If extending, adds one or more strides, but leaves new elements uninitialized.
     *
     * A side effect is that array data is exclusively owned by this instance
     * as if make_exclusive() was called.  This holds even if the size does not change.
     */
    void resize(size_t i) {
        make_data_unique();
        if(i>size()) {
            size_t add=i-size();
            data->strides.push_back(stride(add)); //TODO: split if i too large
            data->tot_size+=add;
        } else { // i<=size()
            size_t drop=size()-i;

            while(i<size()) {
                stride& b=data->strides.back();
                if(drop>=b.size()) {
                    // Drop entire stride
                    data->tot_size -= b.size();
                    drop -= b.size();
                    data->strides.pop_back();

                } else {
                    // Resize stride
                    data->tot_size -= drop;
                    b.m_count -= drop;
                }
            }
        }
    }

    /** @brief Reduce or extend the array size.
     *
     * If extending, adds one or more strides, initializes new elements to v.
     *
     * A side effect is that array data is exclusively owned by this instance
     * as if make_exclusive() was called.  This holds even if the size does not change.
     */
    void resize(size_t i, E v) {
        size_t oldsize=size();
        resize(i);
        if(size()>oldsize)
            std::fill(end()-(size()-oldsize), end(), v);
    }

    //! @brief Ensure that data in the range [A,B) is owned
    //! exclusively by this instance.
    void make_exclusive(iterator A, iterator B)
    {
        make_data_unique();
        for(;A.stride()!=B.stride();A.stride_next())
            A.stride()->make_exclusive();
    }

    //! @brief Ensure that all data is owned exclusively by this instance
    void make_exclusive()
    {
        make_data_unique();
        make_exclusive(begin(), end());
    }

    //! @brief Test if data in the range [A,B) is owned
    //! exclusively by this instance.
    bool is_exclusive(iterator A, iterator B)
    {
        if(!data.unique())
            return false;
        for(;A.stride()!=B.stride();A.stride_next())
            if(!A.stride()->is_exclusive())
                return false;
        return true;
    }

    //! @brief Test if all data is owned exclusively by this instance
    bool is_exclusive()
    {
        return is_exclusive(begin(), end());
    }

    // Access to strides

    stride_iterator stride_begin(){return data->strides.begin();}
    const_stride_iterator stride_begin() const{return data->strides.begin();}

    stride_iterator stride_end(){return data->strides.end();}
    const_stride_iterator stride_end() const{return data->strides.end();}

    //! The number of strides
    size_t strides_size() const{return data->strides.size();}

    //! @brief Append a stride to the end of the array
    void push_back(const stride& s) {
        make_data_unique();
        data->strides.push_back(s);
        data->tot_size += data->strides.back().size();
    }

    //! @brief Remove one stride to the end of the array
    void pop_back() {
        if(data->strides.empty())
            return;
        make_data_unique();
        data->tot_size -= data->strides.back().size();
        data->strides.pop_back();
    }

    // Access to elements

    iterator begin(){return iterator(data->strides.begin(),0);}
    const_iterator begin() const{return const_iterator(data->strides.begin(),0);}

    iterator end(){return iterator(data->strides.end(),0);}
    const_iterator end() const{return const_iterator(data->strides.end(),0);}

    //! O(N) N is number of strides
    iterator find(size_t i) {
        return begin()+i;
    }
};



} // namespace paramTable

#endif // ARRAYDATA_H
