
#if !defined INCLUDED_VT_LB_COMM_COMM_RC_H
#define INCLUDED_VT_LB_COMM_COMM_RC_H

#include <converse.h>
#include <unordered_map>
#include <vector>
#include <mpi.h>
#include <vt-lb/comm/reconverse/reconverse_handle.h>

namespace vt_lb::comm {

struct CommReconverse {
  template <typename T>
  using HandleType = ReconverseHandle<T>;

  CommReconverse() = default;
  CommReconverse(CommReconverse const&) = delete;
  CommReconverse(CommReconverse&&) = delete;
  ~CommReconverse();

public:
  void init(int& argc, char**& argv, MPI_Comm comm = MPI_COMM_NULL);
  void finalize();
  int numRanks() const;
  int getRank() const;
  bool poll() const;
  CommReconverse clone();

  template <typename T>
  ReconverseHandle<T> registerInstanceCollective(T* obj);

  template <typename U, typename V>
  void reduce(int root, MPI_Datatype datatype, MPI_Op op, U sendbuf, V recvbuf, int count);

  template <typename U>
  void broadcast(int root, MPI_Datatype datatype, U buffer, int count);

  template <typename T>
  std::unordered_map<int, std::vector<T>> allgather(T const* sendbuf, int sendcount);

  template <auto fn, typename ProxyT, typename... Args>
  void send(int dest, ProxyT proxy, Args&&... args);

private:
  bool terminated_ = false;
};

} /* end namespace vt_lb::comm */

#include "vt-lb/comm/reconverse/reconverse_handle.impl.h"
#include "vt-lb/comm/reconverse/comm_rc.impl.h"

#endif /*INCLUDED_VT_LB_COMM_COMM_RC_H*/
