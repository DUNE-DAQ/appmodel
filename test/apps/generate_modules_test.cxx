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

#include "confmodel/Connection.hpp"
#include "confmodel/DaqModule.hpp"
#include "confmodel/Session.hpp"

#include "appmodel/ConfigurationHelper.hpp"
#include "appmodel/DFApplication.hpp"
#include "appmodel/DFOApplication.hpp"
#include "appmodel/MLTApplication.hpp"
#include "appmodel/ReadoutApplication.hpp"
#include "appmodel/SmartDaqApplication.hpp"
#include "appmodel/TPReplayApplication.hpp"
#include "appmodel/TPStreamWriterApplication.hpp"
#include "appmodel/TriggerApplication.hpp"

#include "appmodel/DataHandlerModule.hpp"
#include "appmodel/DataReaderModule.hpp"
#include "appmodel/DataMoveCallbackConf.hpp"
#include "appmodel/SocketDataWriterModule.hpp"

#include "appmodel/appmodelIssues.hpp"

#include <string>
using namespace dunedaq;
using namespace dunedaq::appmodel;

int
main(int argc, char* argv[])
{
  if (argc < 4) {
    std::cout << "Usage: " << argv[0] << " <session> <smart-app> <database-file>\n";
    return 0;
  }

  std::string sessionName(argv[1]);
  std::string appName(argv[2]);
  std::string dbfile(argv[3]);

  logging::Logging::setup("test", "generate_module");

  conffwk::Configuration* confdb;
  try {
    confdb = new conffwk::Configuration("oksconflibs:" + dbfile);
  } catch (conffwk::Generic& exc) {
    std::cout << "Failed to load OKS database: " << exc << std::endl;
    return 0;
  }

  auto session = confdb->get<confmodel::Session>(sessionName);
  if (session == nullptr) {
    std::cout << "Failed to get Session " << sessionName << " from database\n";
    return 0;
  }
  auto daqapp = confdb->get<appmodel::SmartDaqApplication>(appName);
  if (daqapp) {
    std::cout << appName << " is of class " << daqapp->class_name() << std::endl;

    auto res = daqapp->cast<confmodel::Resource>();
    if (res && res->is_disabled(*session)) {
      std::cout << "Application " << appName << " is disabled" << std::endl;
      return 0;
    }

    auto helper = std::make_shared<ConfigurationHelper>(session);
    try {
      daqapp->generate_modules(helper);
    }
    catch (appmodel::BadConf& exc) {
      std::cout << "Caught BadConf exception: " << exc << std::endl;
      exit(-1);
    }

    auto modules = daqapp->get_modules();
    std::cout << "Generated " << modules.size() << " modules" << std::endl;
    for (auto daq_module : modules) {
      std::cout << "module " << daq_module->UID() << std::endl;
      daq_module->config_object().print_ref(std::cout, *confdb, "  ");
      std::cout << " input objects " << std::endl;
      for (auto input : daq_module->get_inputs()) {
        auto iObj = input->config_object();
        iObj.print_ref(std::cout, *confdb, "    ");
      }
      std::cout << " output objects " << std::endl;
      for (auto output : daq_module->get_outputs()) {
        auto oObj = output->config_object();
        oObj.print_ref(std::cout, *confdb, "    ");
      }

      auto reader_module = daq_module->cast<appmodel::DataReaderModule>();
      if (reader_module != nullptr) {
        auto callback_confs = reader_module->get_raw_data_callbacks();
        std::cout << " callback confs " << std::endl;
        for (auto* callback_conf : callback_confs) {
          auto cbObj = callback_conf->config_object();
          cbObj.print_ref(std::cout, *confdb, "    ");
        }
      }

      auto handler_module = daq_module->cast<appmodel::DataHandlerModule>();
      if (handler_module != nullptr) {
        auto callback_conf = handler_module->get_raw_data_callback();
        if (callback_conf != nullptr) {
          auto cbObj = callback_conf->config_object();
          cbObj.print_ref(std::cout, *confdb, "    ");
        }
      }

      auto socketwriter_module = daq_module->cast<appmodel::SocketDataWriterModule>();
      if (socketwriter_module != nullptr) {
        auto callback_confs = socketwriter_module->get_raw_data_callbacks();
        std::cout << " callback confs " << std::endl;
        for (auto* callback_conf : callback_confs) {
          auto cbObj = callback_conf->config_object();
          cbObj.print_ref(std::cout, *confdb, "    ");
        }
      }

      std::cout << std::endl;
    }
  } else {
    std::cout << "Failed to get SmartDaqApplication " << appName << " from database\n";
    return 0;
  }
}
