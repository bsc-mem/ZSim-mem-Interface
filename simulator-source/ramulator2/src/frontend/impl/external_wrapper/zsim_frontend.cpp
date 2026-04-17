#include <filesystem>
#include <iostream>
#include <fstream>

#include "frontend/frontend.h"
#include "base/exception.h"

namespace Ramulator {


class ZSim : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, ZSim, "Zsim", "Zsim frontend.")

  public:
    void init() override { 
      num_cores = param<int>("num_cores").required();
    };
    void tick() override { };
    int get_num_cores() override {
        return num_cores;
    }


  private:
    bool is_finished() override { return true; };
    int num_cores;
};

}        // namespace Ramulator