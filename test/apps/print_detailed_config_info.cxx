/**
 * @file print_detailed_config_info.cxx
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "logging/Logging.hpp"

#include "conffwk/Schema.hpp"
#include "conffwk/Configuration.hpp"
#include "conffwk/ConfigurationImpl.hpp"
#include "conffwk/ConfigObject.hpp"
#include "conffwk/ConfigObjectImpl.hpp"

#include "coredal/Session.hpp"
#include "coredal/Connection.hpp"
#include "coredal/DaqModule.hpp"

#include "appdal/DFApplication.hpp"
#include "appdal/DFOApplication.hpp"
#include "appdal/ReadoutApplication.hpp"
#include "appdal/SmartDaqApplication.hpp"
#include "appdal/TriggerApplication.hpp"
#include "appdal/MLTApplication.hpp"
#include "appdal/TPStreamWriterApplication.hpp"

#include "appdal/appdalIssues.hpp"

#include <string>
using namespace dunedaq;

// forward declaration
void print_object_details(conffwk::ConfigObject& config_object_to_print,
                          const std::string& object_name,
                          conffwk::Configuration* confdb,
                          const std::string& spaces);

// function for handling a ConfigObject data member
void print_member_details_if_needed(conffwk::ConfigObject& parent_config_object,
                                    const std::string& member_name,
                                    conffwk::Configuration* confdb,
                                    const std::string& spaces) {
  try {
    conffwk::ConfigObject member_object;
    parent_config_object.get(member_name, member_object);
    if (! member_object.is_null()) {
      print_object_details(member_object, member_name, confdb, spaces + "  ");
    }
  }
  catch (conffwk::Exception& exc) {
    try {
      std::vector<conffwk::ConfigObject> member_object_list;
      parent_config_object.get(member_name, member_object_list);
      for (uint32_t idx = 0; idx < member_object_list.size(); ++idx) {
        if (! member_object_list[idx].is_null()) {
          if (member_name != "inputs" && member_name != "outputs") {
            std::ostringstream oss_name;
            oss_name << member_name << "[" << idx << "]";
            print_object_details(member_object_list[idx], oss_name.str(), confdb, spaces + "  ");
          }
        }
      }
    }
    catch (conffwk::Exception& exc) {
    }
  }
}

// function for printing out the details of a specified ConfigObject
void print_object_details(conffwk::ConfigObject& config_object_to_print,
                          const std::string& object_name,
                          conffwk::Configuration* confdb,
                          const std::string& spaces) {
  if (object_name != "") {
    std::cout << spaces << "-----" << std::endl;
    std::cout << spaces << "\"" << object_name << "\" ";
  }
  config_object_to_print.print_ref(std::cout, *confdb, spaces);
  dunedaq::conffwk::class_t cd = confdb->get_class_info(config_object_to_print.class_name());
  for (const auto& attr : cd.p_attributes) {
    const std::string& attr_name(attr.p_name);
    print_member_details_if_needed(config_object_to_print, attr_name, confdb, spaces);
  }
  for (const auto& relship : cd.p_relationships) {
    const std::string& rel_name(relship.p_name);
    print_member_details_if_needed(config_object_to_print, rel_name, confdb, spaces);
  }
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::cout << "Usage: " << argv[0] << " <session> <smart-app> <database-file>\n";
    return 0;
  }
  logging::Logging::setup();

  std::string sessionName(argv[1]);
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

  auto session = confdb->get<coredal::Session>(sessionName);
  if (session == nullptr) {
    std::cout << "Failed to get Session " << sessionName
              << " from database\n";
    return 0;
  }
  auto daqapp = confdb->get<appdal::SmartDaqApplication>(appName);
  if (daqapp) {
    std::cout << appName << " is of class " << daqapp->class_name() << std::endl;

    auto res = daqapp->cast<coredal::ResourceBase>();
    if (res && res->disabled(*session)) {
      std::cout << "Application " << appName << " is disabled" << std::endl;
      return 0;
    }
    std::vector<const coredal::DaqModule*> modules;
    try {
      modules = daqapp->generate_modules(confdb, dbfile, session);
    }
    catch (appdal::BadConf& exc) {
      std::cout << "Caught BadConf exception: " << exc << std::endl;
      exit(-1);
    }

    std::cout << "Generated " << modules.size() << " modules" << std::endl;
    for (auto module: modules) {
      std::cout << "module " << module->UID() << std::endl;
      conffwk::ConfigObject module_config_object = module->config_object();
      print_object_details(module_config_object, "", confdb, "  ");
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
