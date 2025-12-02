
#include "vt-lb/comm/reconverse/comm_rc.h"

namespace vt_lb::comm {

void CommReconverse::init(int& argc, char**& argv) {
  // initates backend, rdma, cmistartthreads(),
  //  and converseRunPe (starts scheduler on every PE)
  int usched = 1; // user scheduling, Converse init doesn't start scheduler
  int initret = 1; // needs to return from initi without calling ConverseExit
  ConverseInit(argc, argv, START_FUNCTION, usched, initret);

  // TODO: is start function still used?
}

void CommReconverse::finalize() {
  ConverseExit();
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
