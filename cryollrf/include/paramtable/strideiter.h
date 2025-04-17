#ifndef STRIDEITER_H
#define STRIDEITER_H

#include <iterator>

namespace paramTable {

namespace detail {
    template<typename T> struct remove_const          {typedef T type;};
    template<typename T> struct remove_const<const T> {typedef T type;};

    template<bool B, class T = void> struct enable_if {};
    template<class T> struct enable_if<true,T> { typedef T type; };

    template<typename T> struct is_const  {enum{value=0};};
    template<typename T> struct is_const<const T> {enum{value=1};};

    struct invalid {};
}

template<typename E>
class array_data;

template<typename E, typename SI>
class array_iterator : public std::iterator<std::random_access_iterator_tag, E> {
    /* Positions for chunk in [cbegin, cend)
     *
     * begin: array_iterator(cbegin,0)
     * end:   array_iterator(cend,0)
     *
     * End of first chunk: array_iterator(cbegin, cbegin->size()-1)
     * Beginning of second chunk: array_iterator(cbegin+1, 0)
     */
    SI chunk;
    size_t pos;

//    template<typename F>
//    friend class array_data;

    typedef std::iterator<std::random_access_iterator_tag, E> base_class;
    typedef typename detail::remove_const<E>::type mutable_value_type;
public:
    typedef typename base_class::difference_type difference_type;

    //! @brief The current stride iterator
    const SI& stride() const{return chunk;}
    //! @brief The current position inside the stride
    size_t position() const{return pos;}

    // Create uninitialized
    array_iterator(): chunk(), pos(0) {}
    ~array_iterator(){}

    // Create from exact chunk iterator
    array_iterator(const SI& c, size_t p): chunk(c), pos(p) {}

    // Create from compatible chunk iterator (iterator -> const_iterator)
//    template<typename SIX>
//    array_iterator(const SIX& c, size_t p): chunk(c), pos(p) {}

    // Copy from same
    array_iterator(const array_iterator& o): chunk(o.chunk), pos(o.pos) {}

    // Copy from non-const iterator (iterator -> const_iterator)
//    array_iterator(const SIC &o)
//        : chunk(o.stride()), pos(o.position())
//    {}

    // Assign from same
    array_iterator& operator=(const array_iterator& o) {
        chunk=o.chunk;
        pos=o.pos;
        return *this;
    }

    // Assign from non-const iterator (iterator -> const_iterator)
//    template<typename SX>
//    array_iterator& operator=(const SIC &o) {
//        chunk=o.chunk;
//        pos=o.pos;
//        return *this;
//    }

    //! O(1)
    array_iterator& operator++() {
        if(pos+1==chunk->size()) {
            // Cross chunk boundary
            ++chunk;
            pos=0;
        } else
            pos++;
        return *this;
    }

    //! O(1)
    array_iterator operator++(int) {
        array_iterator save(*this);
        ++(*this);
        return save;
    }

    //! O(1)
    array_iterator& operator--() {
        if(pos==0) {
            // Cross chunk boundary
            --chunk;
            pos=chunk->size()-1;
        } else
            pos--;
        return *this;
    }

    //! O(1)
    array_iterator operator--(int) {
        array_iterator save(*this);
        --(*this);
        return save;
    }

    bool operator==(const array_iterator& o) const {
        return chunk==o.chunk && pos==o.pos;
    }

    bool operator!=(const array_iterator& o) const { return !(*this==o);}

    //! O(1)
    E& operator*() const {return (*chunk)[pos];}
    //E* operator->() {return &(*chunk)[pos];}

    //! O(N) N is number of chunk boundaries crossed
    array_iterator& operator+=(size_t i) {
        while(i) {
            if(i+pos >= chunk->size()) {
                // Step forward by a whole chunk
                i-=chunk->size()-pos;
                ++chunk;
                pos=0;
            } else {
                // On the correct chunk.  Move to offset
                pos+=i;
                break;
            }
        }
        return *this;
    }

    //! O(N) N is number of chunk boundaries crossed
    const array_iterator operator+(size_t i) const {
        array_iterator save(*this);
        save+=i;
        return save;
    }

    //! O(N) N is number of chunk boundaries crossed
    array_iterator& operator-=(size_t i) {
        while(true) {
            if(i>pos) {
                // Step back to the previous chunk
                i-=pos;  // discount remainder of current chunk
                --chunk;
                pos=chunk->size(); // This is ok since i>0
            } else {
                // On the correct chunk.  Move to offset
                pos-=i;
                break;
            }
        }
        return *this;
    }

    //! O(N) N is number of chunk boundaries crossed
    const array_iterator operator-(size_t i) const {
        array_iterator save(*this);
        save-=i;
        return save;
    }

    //! O(N) N is number of chunk boundaries crossed
    difference_type operator-(const array_iterator& o) const
    {
        difference_type diff=0;
        bool neg=false;
        array_iterator a,b;

        if(o>(*this)) {
            a=o; b=*this;
        } else {
            a=*this; b=o;
            neg=true;
        }
        // Now b > a
        while(a.chunk!=b.chunk) {
            diff += a->size() - a.pos;
            a.pos=0;
            ++a.chunk;
        }
        diff += b.pos-a.pos;
        return neg ? diff : -diff;
    }

    bool operator<(const array_iterator& o) const{
        return chunk==o.chunk ? pos<o.pos : chunk<o.chunk;
    }
    bool operator>=(const array_iterator& o) const { return !(*this<o);}

    bool operator>(const array_iterator& o) const{
        return chunk==o.chunk ? pos>o.pos : chunk>o.chunk;
    }
    bool operator<=(const array_iterator& o) const { return !(*this>o);}

    bool operator[](size_t i) const { return (*chunk)[pos+i];}

    // special operations

    bool valid() const{return chunk!=SI();}

    void stride_next() {
        chunk++;
        pos=0;
    }

    void stride_prev() {
        chunk--;
        pos=0;
    }
};

template<typename E, typename SI>
bool operator+(size_t i, array_iterator<E,SI>& a) { return a+i; }

template<typename E, typename SI>
bool operator<(size_t i, array_iterator<E,SI>& a) { return a>i; }

template<typename E, typename SI>
bool operator<=(size_t i, array_iterator<E,SI>& a) { return a>=i; }

template<typename E, typename SI>
bool operator>(size_t i, array_iterator<E,SI>& a) { return a<i; }

template<typename E, typename SI>
bool operator>=(size_t i, array_iterator<E,SI>& a) { return a<=i; }

} // namespace paramTable

#endif // STRIDEITER_H
