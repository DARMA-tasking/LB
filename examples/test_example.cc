/*
//@HEADER
// *****************************************************************************
//
//                              test_example.h
//                 DARMA/vt-lb => Virtual Transport/Load Balancers
//
// Copyright 2019-2024 National Technology & Engineering Solutions of Sandia, LLC
// (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from this
//   software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact darma@sandia.gov
//
// *****************************************************************************
//@HEADER
*/

#include <vt-lb/algo/driver/driver.h>
#include <vt-lb/algo/temperedlb/configuration.h>
#include <vt-lb/model/PhaseData.h>

#include <comm/comm/MPI/comm_mpi.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {

using Distribution = std::unordered_map<
  vt_lb::model::RankType, std::vector<vt_lb::model::TaskType>
>;

std::unique_ptr<vt_lb::model::PhaseData> makePhaseData(
  vt_lb::model::RankType rank
) {
  using vt_lb::model::PhaseData;
  using vt_lb::model::Task;
  using vt_lb::model::TaskMemory;
  using vt_lb::model::TaskType;

  constexpr int tasks_per_rank = 4;
  auto phase_data = std::make_unique<PhaseData>(rank);

  for (int task_index = 0; task_index < tasks_per_rank; ++task_index) {
    auto const task_id = static_cast<TaskType>(rank * tasks_per_rank + task_index);
    auto const load = rank == 0 ? 10.0 : 1.0;

    phase_data->addTask(Task{
      task_id,
      rank,
      rank,
      true,
      TaskMemory{},
      load
    });
  }

  return phase_data;
}

void printDistribution(Distribution const& distribution, int num_ranks) {
  std::cout << "Final task distribution:\n";
  for (int rank = 0; rank < num_ranks; ++rank) {
    auto tasks = distribution.at(rank);
    std::sort(tasks.begin(), tasks.end());

    std::cout << "  rank " << rank << ':';
    for (auto const task : tasks) {
      std::cout << ' ' << task;
    }
    std::cout << '\n';
  }
}

} // end anonymous namespace

int main(int argc, char** argv) {
  comm::CommMPI comm;
  comm.init(argc, argv);

  vt_lb::algo::temperedlb::Configuration config{comm.numRanks()};
  config.deterministic_ = true;
  config.seed_ = 97;
  config.work_model_.beta = 0.0;
  config.num_iters_ = 8;
  config.num_trials_ = 1;

  auto const distribution = vt_lb::runLBAllGather(
    vt_lb::DriverAlgoEnum::TemperedLB,
    comm,
    config,
    makePhaseData(comm.getRank())
  );

  if (comm.getRank() == 0) {
    printDistribution(distribution, comm.numRanks());
  }

  comm.finalize();
  return 0;
}
