
#include "vt-lb/comm/reconverse/comm_rc.h"

namespace vt_lb::comm {

void CommReconverse::init(int& argc, char**& argv, MPI_Comm comm) {
  // initates backend, rdma, cmistartthreads(),
  //  and converseRunPe (starts scheduler on every PE)

  int usched = 1; // user scheduling, Converse init doesn't start scheduler
  int initret = 1; // needs to return from init without calling ConverseExit

  int num_ranks;
  MPI_Comm mpi_comm = comm;
  MPI_Comm_size(mpi_comm, &num_ranks);

  char **converse_argv = new char*[argc + 2];
  for (int i = 0; i < argc; ++i) {
    converse_argv[i] = argv[i];
  }
  converse_argv[argc] = const_cast<char*>("+p");
  converse_argv[argc + 1] = const_cast<char*>(std::to_string(num_ranks).c_str());

  ConverseInit(argc + 2, converse_argv, NULL, usched, initret);
  // TODO: is start function still used?
}

void CommReconverse::finalize() {
  CmiExit(0);
}

CommReconverse CommReconverse::clone() {
  // TODO: what to do here?
}

CommReconverse::~CommReconverse() {
}

int CommReconverse::numRanks() const {
  return CmiNumPes();
}

int CommReconverse::getRank() const {
  return CmiMyPe();
}

bool CommReconverse::poll() const {
  CsdSchedulePoll(); // returns when queues are empty
  return false;
}

} // namespace vt_lb::comm
