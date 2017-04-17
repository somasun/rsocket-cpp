#pragma once

#include "reactivestreams/ReactiveStreams.h"

#include "Refcounted.h"
#include "Subscription.h"

namespace yarpl {

template <typename T>
class Subscriber : public reactivestreams_yarpl::Subscriber<T>,
                   public virtual Refcounted {
 public:
  // Note: if any of the following methods is overridden in a subclass,
  // the new methods SHOULD ensure that these are invoked as well.
  virtual void onSubscribe(Reference<Subscription> subscription) {
    subscription_ = subscription;
  }

  // No further calls to the subscription after this method is invoked.
  virtual void onComplete() {
    subscription_.reset();
  }

  // No further calls to the subscription after this method is invoked.
  virtual void onError(const std::exception_ptr) {
    subscription_.reset();
  }

  virtual void onNext(const T&) {}
  virtual void onNext(T&& value) {
    onNext(value);
  }

 protected:
  Subscription* subscription() {
    return subscription_.operator->();
  }

 private:
  // "Our" reference to the subscription, to ensure that it is retained
  // while calls to its methods are in-flight.
  Reference<Subscription> subscription_{nullptr};

  // Note: we've overridden the signature of onSubscribe with yarpl's
  // Subscriber.  Keep this definition, making it private, to keep the
  // compiler from issuing a warning about the override.
  virtual void onSubscribe(reactivestreams_yarpl::Subscription*) {}
};

} // yarpl
