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

#include "coredal/Session.hpp"
#include "coredal/Connection.hpp"
#include "coredal/DaqModule.hpp"

#include "appdal/DFApplication.hpp"
#include "appdal/DFOApplication.hpp"
#include "appdal/ReadoutApplication.hpp"
#include "appdal/SmartDaqApplication.hpp"
#include "appdal/TPWriterApplication.hpp"

#include "appdal/appdalIssues.hpp"

#include <string>
using namespace dunedaq;

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::cout << "Usage: " << argv[0] << " <session> <readout-app> <database-file>\n";
    return 0;
  }
  logging::Logging::setup();

  std::string sessionName(argv[1]);
  std::string appName(argv[2]);
  std::string dbfile(argv[3]);
  auto confdb = new oksdbinterfaces::Configuration("oksconfig:" + dbfile);

  auto session = confdb->get<coredal::Session>(sessionName);
  if (session == nullptr) {
    std::cout << "Failed to get Session " << sessionName
              << " from database\n";
    return 0;
  }
  auto daqapp = confdb->get<coredal::Application>(appName);
  if (daqapp) {
    std::cout << appName << " is of class " << daqapp->class_name() << std::endl;

    auto res = daqapp->cast<coredal::ResourceBase>();
    if (res && res->disabled(*session)) {
      std::cout << "Application " << appName << " is disabled" << std::endl;
      return 0;
    }
    std::vector<const coredal::DaqModule*> modules;
    auto smart = daqapp->cast<appdal::SmartDaqApplication>();
    if (smart) {
      try {
        modules = smart->generate_modules(confdb, dbfile, session);
      }
      catch (appdal::BadConf& exc) {
        std::cout << "Caught BadConf exception: " << exc << std::endl;
        exit(-1);
      }
    }
    else {
      std::cout << appName << " failed to cast to SmartDaqApplication\n";
      return 0;
    }

    std::cout << "Generated " << modules.size() << " modules" << std::endl;
    for (auto module: modules) {
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
