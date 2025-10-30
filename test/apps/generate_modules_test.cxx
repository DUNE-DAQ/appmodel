/**
 * @file generate_modules_test.cxx
 *
 * Test/demonstration of SmartDaqApplication's generate_modules() method
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "logging/Logging.hpp"

#include "conffwk/Configuration.hpp"

#include "confmodel/Session.hpp"
#include "confmodel/Connection.hpp"
#include "confmodel/DaqModule.hpp"

#include "appmodel/DFApplication.hpp"
#include "appmodel/DFOApplication.hpp"
#include "appmodel/ReadoutApplication.hpp"
#include "appmodel/SmartDaqApplication.hpp"
#include "appmodel/TriggerApplication.hpp"
#include "appmodel/MLTApplication.hpp"
#include "appmodel/TPReplayApplication.hpp"
#include "appmodel/TPStreamWriterApplication.hpp"

#include "appmodel/appmodelIssues.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace dunedaq;

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0] << " <session> <smart-app> <database-file>\n";
    return EXIT_FAILURE;
  }

  const std::string sessionName(argv[1]);
  const std::string appName(argv[2]);
  const std::string dbfile(argv[3]);

  logging::Logging::setup("test", "generate_module");

  std::unique_ptr<conffwk::Configuration> confdb;
  try {
    confdb = std::make_unique<conffwk::Configuration>("oksconflibs:" + dbfile);
  }
  catch (const conffwk::Generic& exc) {
    std::cerr << "Failed to load OKS database: " << exc << std::endl;
    return EXIT_FAILURE;
  }

  const auto* session = confdb->get<confmodel::Session>(sessionName);
  if (session == nullptr) {
    std::cerr << "Failed to get Session \"" << sessionName
              << "\" from database\n";
    return EXIT_FAILURE;
  }

  const auto* daqapp = confdb->get<appmodel::SmartDaqApplication>(appName);
  if (daqapp == nullptr) {
    std::cerr << "Failed to get SmartDaqApplication \"" << appName
              << "\" from database\n";
    return EXIT_FAILURE;
  }

  std::cout << "Application \"" << appName << "\" is of class " 
            << daqapp->class_name() << std::endl;

  const auto* res = daqapp->cast<confmodel::Resource>();
  if (res && res->is_disabled(*session)) {
    std::cout << "Application \"" << appName << "\" is disabled in session \"" 
              << sessionName << "\"" << std::endl;
    return EXIT_SUCCESS;
  }

  try {
    // Note: generate_modules is non-const, so we need a non-const pointer
    auto* non_const_daqapp = const_cast<appmodel::SmartDaqApplication*>(daqapp);
    non_const_daqapp->generate_modules(session);
  }
  catch (const appmodel::BadConf& exc) {
    std::cerr << "ERROR: Caught BadConf exception during module generation: " 
              << exc << std::endl;
    return EXIT_FAILURE;
  }
  catch (const std::exception& exc) {
    std::cerr << "ERROR: Unexpected exception during module generation: " 
              << exc.what() << std::endl;
    return EXIT_FAILURE;
  }

  const auto modules = daqapp->get_modules();
  std::cout << "\nGenerated " << modules.size() << " module(s)" << std::endl;
  std::cout << std::string(60, '-') << std::endl;

  for (const auto* daq_module : modules) {
    if (daq_module == nullptr) {
      std::cerr << "WARNING: Encountered null module pointer" << std::endl;
      continue;
    }

    std::cout << "\nModule: " << daq_module->UID() << std::endl;
    daq_module->config_object().print_ref(std::cout, *confdb, "  ");

    const auto inputs = daq_module->get_inputs();
    std::cout << "  Input connections: " << inputs.size() << std::endl;
    for (const auto* input : inputs) {
      if (input) {
        const auto& iObj = input->config_object();
        iObj.print_ref(std::cout, *confdb, "    ");
      }
    }

    const auto outputs = daq_module->get_outputs();
    std::cout << "  Output connections: " << outputs.size() << std::endl;
    for (const auto* output : outputs) {
      if (output) {
        const auto& oObj = output->config_object();
        oObj.print_ref(std::cout, *confdb, "    ");
      }
    }
    std::cout << std::endl;
  }

  std::cout << std::string(60, '-') << std::endl;
  std::cout << "Test completed successfully" << std::endl;

  return EXIT_SUCCESS;
}

