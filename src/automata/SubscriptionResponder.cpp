// Copyright 2004-present Facebook. All Rights Reserved.

#include "src/automata/SubscriptionResponder.h"
#include "src/Frame.h"

namespace reactivesocket {

void SubscriptionResponder::onCompleteImpl() {
  LOG(FATAL) << "onComplete is not allowed on Subscription iteraction.";
}

void SubscriptionResponder::onNextFrame(Frame_REQUEST_SUB&& frame) {
  processRequestN(frame);
}
}
