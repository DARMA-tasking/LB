#include <vt-lb/comm/MPI/comm_mpi.h>
#include <vt-lb/comm/VT/comm_vt.h>
#include <vt-lb/algo/driver/driver.h>

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
  //config.k_max_ = 1;

  vt_lb::model::PhaseData phase_data{comm.getRank()};
  for (int i = 0; i < 5; ++i) {
    vt_lb::model::Task task{
      (uint64_t)(i + comm.getRank() * 10),
      comm.getRank(),
      comm.getRank(),
      true,
      vt_lb::model::TaskMemory{1024, 512, 256},
      10.0 + i + comm.getRank()
    };
    phase_data.addTask(task);
  }

  vt_lb::runLB(
    vt_lb::DriverAlgoEnum::TemperedLB,
    comm,
    config,
    std::make_unique<vt_lb::model::PhaseData>(phase_data)
  );

  while (comm.poll()) {
  }

  printf("out of poll\n");

  comm.finalize();
  return 0;
}
