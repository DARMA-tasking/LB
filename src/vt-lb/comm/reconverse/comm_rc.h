
#if !defined INCLUDED_VT_LB_COMM_COMM_VT_H
#define INCLUDED_VT_LB_COMM_COMM_VT_H

namespace vt_lb::comm {

struct CommReconverse {
  template <typename T>

  CommReconverse() = default;
  CommReconverse(CommReconverse const&) = delete;
  CommReconverse(CommReconverse&&) = delete;
  ~CommReconverse();

public:
  void init(int& argc, char**& argv);
  void finalize();
  int numRanks() const;
  int getRank() const;
  bool poll() const;
  CommReconverse clone();

  template <typename T>
  ClassHandle<T> registerInstanceCollective(T* obj);

  template <auto fn, typename ProxyT, typename... Args>
  void send(vt::NodeType dest, ProxyT proxy, Args&&... args);

private:
  bool terminated_ = false;
};

} /* end namespace vt_lb::comm */

#include "vt-lb/comm/reconverse/comm_rc.impl.h"

#endif /*INCLUDED_VT_LB_COMM_COMM_VT_H*/
