/**
 * @file gen_readout_modules.cxx
 *
 * Quick test/demonstration of ReadoutApplication's dal method
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "logging/Logging.hpp"

#include "oksdbinterfaces/Configuration.hpp"

#include "coredal/Connection.hpp"

#include "readoutdal/DataReader.hpp"
#include "readoutdal/DataStreamDesccriptor.hpp"
#include "readoutdal/DLH.hpp"
#include "readoutdal/LinkHandlerConf.hpp"
#include "readoutdal/QueueConnectionRule.hpp"
#include "readoutdal/QueueDescriptor.hpp"
#include "readoutdal/ReadoutApplication.hpp"

#include <string>

using namespace dunedaq;

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::cout << "Usage: " << argv[0] << " <readout-app> <database-file>\n";
    return 0;
  }
  logging::Logging::setup();

  std::string dbfile(argv[2]);
  auto confdb = new oksdbinterfaces::Configuration("oksconfig:" + dbfile);
  std::string appName(argv[1]);

  std::vector<const readoutdal::DataReader*> dataReaders;
  std::vector<const readoutdal::DLH*> dataHandlers;

  auto daqapp = confdb->get<readoutdal::ReadoutApplication>(appName);
  if (daqapp) {
    for (auto module: daqapp->generate_modules(confdb, dbfile)) {
      std::cout << "module " << module->UID() << std::endl;
      module->config_object().print_ref(std::cout, *confdb, "  ");
      std::cout  << " input objects "  << std::endl;
      for (auto input : module->get_inputs()) {
        auto iObj = input->config_object();
        iObj.print_ref(std::cout, *confdb, "    ");
      }
      std::cout  << " output objects "  << std::endl;
      for (auto output : module->get_outputs()) {
        auto oObj = output->config_object();
        oObj.print_ref(std::cout, *confdb, "    ");
      }
      std::cout << std::endl;
    }
  }
  else {
    std::cout << "Failed to get ReadoutApplication " << appName
              << " from database\n";
    return 0;
  }
}
