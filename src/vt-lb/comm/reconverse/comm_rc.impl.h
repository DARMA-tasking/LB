#if !defined INCLUDED_VT_LB_COMM_COMM_VT_IMPL_H
#define INCLUDED_VT_LB_COMM_COMM_VT_IMPL_H

#include "vt-lb/comm/reconverse/comm_rc.h"

namespace vt_lb::comm {

template <typename T>
auto CommReconverse::registerInstanceCollective(T* obj) {
  
}

template <auto fn, typename ProxyT, typename... Args>
void CommReconverse::send(vt::NodeType dest, ProxyT proxy, Args&&... args) {
  // create message
  // set size
  // set handler
  // set src pe
  
  CmiSyncSendAndFree(dest, msgsize, msg);
}

} // namespace vt_lb::comm

#include "vt-lb/comm/VT/proxy_wrapper.impl.h"

#endif /* INCLUDED_VT_LB_COMM_COMM_VT_IMPL_H */
