#ifndef CBLIST_H
#define CBLIST_H

#include <stdexcept>
#include <set>

#include <errlog.h>

#include <tr1/functional>

namespace paramTable {

template<class C>
class callback_list;

namespace detail {
    //! Un-typed subscription.
    //! Allows disconnect() without knowing the type.
    struct subscription {
        virtual void disconnect()=0;
        virtual ~subscription();
    };

    //! Holds a subscriber callback object
    template<class C>
    class typed_subscription : public subscription {
        friend class callback_list<C>;

        typedef callback_list<C> list_type;
        typedef std::tr1::function<void(const C&)> callback_type;

        virtual void disconnect()
        {
            owner->disconnect(this);
            delete this;
        }

        list_type * const owner;
        const callback_type cb;

        typed_subscription(list_type& o, const callback_type& c)
            : owner(&o), cb(c) {}

        virtual ~typed_subscription(){}
    };
}

typedef detail::subscription* subscription_type;

/** Subscriber callback list.
 *
 * Holds a list of subscriptions.
 */
template<class C>
class callback_list
{
    typedef detail::typed_subscription<C> typed_subscription;

    typedef std::set<typed_subscription*> subscribers_t;
    subscribers_t subscribers;

public:
    typedef C object_type;
    typedef typename typed_subscription::callback_type callback_type;

    ~callback_list()
    {
        while(!subscribers.empty())
            (*subscribers.begin())->disconnect();
    }

    //! Create a new subscription
    typed_subscription* connect(const callback_type& arg)
    {
        typed_subscription *s=new typed_subscription(*this, arg);
        if(subscribers.insert(s).second)
            return s;
        delete s;
        throw std::logic_error("Duplicate subscription");
    }

    //! Cancel an existing subscription.
    //! After this call the callback will not be invoked.
    void disconnect(typed_subscription* subs)
    {
        if(subscribers.erase(subs)==0)
            throw std::logic_error("Not it subscription list");
    }

    //! Pass the argument to all subscribers
    void operator()(const C& o)
    {
        for(typename subscribers_t::const_iterator it=subscribers.begin();
            it!=subscribers.end();
            ++it)
        {
            (*it)->cb(o);
        }
    }
};

} // namespace paramTable

#endif // CBLIST_H
