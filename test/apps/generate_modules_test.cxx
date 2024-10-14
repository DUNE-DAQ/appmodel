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

#include "conffwk/Configuration.hpp"

#include "confmodel/System.hpp"
#include "confmodel/Connection.hpp"
#include "confmodel/DaqModule.hpp"

#include "appmodel/DFApplication.hpp"
#include "appmodel/DFOApplication.hpp"
#include "appmodel/ReadoutApplication.hpp"
#include "appmodel/SmartDaqApplication.hpp"
#include "appmodel/TriggerApplication.hpp"
#include "appmodel/MLTApplication.hpp"
#include "appmodel/TPStreamWriterApplication.hpp"

#include "appmodel/appmodelIssues.hpp"

#include <string>
using namespace dunedaq;

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::cout << "Usage: " << argv[0] << " <system> <smart-app> <database-file>\n";
    return 0;
  }
  logging::Logging::setup();

  std::string systemName(argv[1]);
  std::string appName(argv[2]);
  std::string dbfile(argv[3]);
  conffwk::Configuration* confdb;
  try {
    confdb = new conffwk::Configuration("oksconflibs:" + dbfile);
  }
  catch (conffwk::Generic& exc) {
    std::cout << "Failed to load OKS database: " << exc << std::endl;
    return 0;
  }

  auto system = confdb->get<confmodel::System>(systemName);
  if (system == nullptr) {
    std::cout << "Failed to get System " << systemName
              << " from database\n";
    return 0;
  }
  auto daqapp = confdb->get<appmodel::SmartDaqApplication>(appName);
  if (daqapp) {
    std::cout << appName << " is of class " << daqapp->class_name() << std::endl;

    auto res = daqapp->cast<confmodel::ResourceBase>();
    if (res && res->disabled(*system)) {
      std::cout << "Application " << appName << " is disabled" << std::endl;
      return 0;
    }
    std::vector<const confmodel::DaqModule*> modules;
    try {
      modules = daqapp->generate_modules(confdb, dbfile, system);
    }
    catch (appmodel::BadConf& exc) {
      std::cout << "Caught BadConf exception: " << exc << std::endl;
      exit(-1);
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
    std::cout << "Failed to get SmartDaqApplication " << appName
              << " from database\n";
    return 0;
  }
}
