#include <vt-lb/comm/MPI/comm_mpi.h>
#include <vt-lb/comm/vt/comm_vt.h>
#include <vt-lb/algo/driver/driver.h>
#include <random>

struct MyClass {
  void myHandler(int a, double b) {
    // Handler implementation
    printf("handler says: a=%d, b=%f\n", a, b);
  }
  void myHandler2(std::string msg) {
    // Another handler implementation
    printf("string is %s\n", msg.c_str());
  }
};

// Helper to sample wide-range communication volumes
static double sampleCommVolume(std::mt19937& gen) {
  std::uniform_real_distribution<double> pick(0.0, 1.0);
  double p = pick(gen);
  if (p < 0.4) {
    std::uniform_real_distribution<double> d(1.0, 100.0);
    return d(gen);
  } else if (p < 0.8) {
    std::uniform_real_distribution<double> d(100.0, 10000.0);
    return d(gen);
  } else {
    std::uniform_real_distribution<double> d(10000.0, 1000000.0);
    return d(gen);
  }
}

// Synthetic graph builder (no ghost tasks: edges reference real remote tasks)
static void buildTestGraph(vt_lb::model::PhaseData& pd, int rank, int num_ranks) {
  using namespace vt_lb::model;
  std::mt19937 gen(rank * 7937 + 17 + 9);
  std::uniform_real_distribution<double> uni(0.0, 1.0);

  (void)num_ranks;

  int const local_tasks = 10 + rank*2; // Varying number of tasks per rank
  std::vector<TaskType> local_ids;
  local_ids.reserve(local_tasks);

  // Local tasks
  for (int i = 0; i < local_tasks; ++i) {
    TaskType tid = static_cast<TaskType>(rank * 100 + i);
    local_ids.push_back(tid);
    Task t{
      tid,
      rank,
      rank,
      true,
      TaskMemory{(double)(1024 + i * 10), 512, 256},
      20.0 + i
    };
    pd.addTask(t);
  }

  // Intra-rank communications (possibly asymmetric)
  for (size_t i = 0; i < local_ids.size(); ++i) {
    int count = 0;
    for (size_t j = i + 1; j < local_ids.size(); ++j) {
      if (uni(gen) > 0.9 && count < 5) {
        double vol = sampleCommVolume(gen);
        pd.addCommunication(Edge{local_ids[i], local_ids[j], vol, rank, rank});
        count++;
        // if (uni(gen) > 0.65) {
        //   // Reverse with scaled volume to introduce asymmetry
        //   pd.addCommunication(Edge{local_ids[j], local_ids[i], vol * (0.1 + uni(gen)), rank, rank});
        // }
      }
    }
  }

  // // Cross-rank asymmetric communications referencing actual remote task IDs
  // for (int r = 0; r < num_ranks; ++r) {
  //   if (r == rank) continue;
  //   for (auto lid : local_ids) {
  //     for (int i = 0; i < local_tasks; ++i) {
  //       TaskType rid = static_cast<TaskType>(r * 100 + i);
  //       if (uni(gen) > 0.7) {
  //         double vol = sampleCommVolume(gen);
  //         pd.addCommunication(Edge{lid, rid, vol, rank, r});
  //         if (uni(gen) > 0.8) {
  //           pd.addCommunication(Edge{rid, lid, vol * (0.05 + uni(gen)), r, rank});
  //         }
  //       }
  //     }
  //   }
  // }
}

int main(int argc, char** argv) {
  auto comm = vt_lb::comm::CommMPI();
  comm.init(argc, argv);

  //auto cls = std::make_unique<MyClass>();
  //auto handle = comm.registerInstanceCollective(cls.get());
  //auto rank = comm.getRank();

  // int value = 10;
  // int recv_value = 0;
  // handle.reduce(1, MPI_INT, MPI_SUM, &value, &recv_value, 1);

  // fmt::print("Rank {}: reduced value is {}\n", rank, recv_value);

  // if (rank == 0) {
  //   handle[1].send<&MyClass::myHandler2>(std::string{"hello from rank 0"});
  // }
  // if (rank == 1) {
  //   handle[0].send<&MyClass::myHandler>(2, 10.3);
  // }

  printf("%d: Running runLB\n", comm.getRank());

  vt_lb::algo::temperedlb::Configuration config{comm.numRanks()};
  config.deterministic_ = true;
  config.seed_ = 97;
  config.visualize_task_graph_ = true;
  config.visualize_clusters_ = true;
  config.cluster_based_on_communication_ = true;
  config.work_model_.beta = 0.5;
  config.visualize_full_graph_ = true;
  config.num_iters_ = 2;
  config.num_trials_ = 1;
  //config.k_max_ = 1;

  vt_lb::model::PhaseData phase_data{comm.getRank()};
  // Remove previous simple task init; use synthetic graph
  buildTestGraph(phase_data, comm.getRank(), comm.numRanks());

  vt_lb::runLB(
    vt_lb::DriverAlgoEnum::TemperedLB,
    comm,
    config,
    std::make_unique<vt_lb::model::PhaseData>(phase_data)
  );

  comm.finalize();
  return 0;
}
