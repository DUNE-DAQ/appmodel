/**
 * @file print_detailed_config_info.cxx
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "logging/Logging.hpp"

#include "conffwk/ConfigObject.hpp"
#include "conffwk/ConfigObjectImpl.hpp"
#include "conffwk/Configuration.hpp"
#include "conffwk/ConfigurationImpl.hpp"
#include "conffwk/Schema.hpp"

#include "confmodel/Connection.hpp"
#include "confmodel/DaqModule.hpp"
#include "confmodel/Session.hpp"

#include "appmodel/DFApplication.hpp"
#include "appmodel/DFOApplication.hpp"
#include "appmodel/MLTApplication.hpp"
#include "appmodel/ReadoutApplication.hpp"
#include "appmodel/SmartDaqApplication.hpp"
#include "appmodel/TPStreamWriterApplication.hpp"
#include "appmodel/TriggerApplication.hpp"

#include "appmodel/appmodelIssues.hpp"

#include <string>
using namespace dunedaq;

// forward declaration
void
print_object_details(conffwk::ConfigObject& config_object_to_print,
                     const std::string& object_name,
                     conffwk::Configuration* confdb,
                     const std::string& spaces,
                     std::vector<std::string>& list_of_applications);

// function for handling a ConfigObject data member
void
print_member_details_if_needed(conffwk::ConfigObject& parent_config_object,
                               const std::string& member_name,
                               conffwk::Configuration* confdb,
                               const std::string& spaces,
                               std::vector<std::string>& list_of_applications)
{
  try {
    conffwk::ConfigObject member_object;
    parent_config_object.get(member_name, member_object);
    if (!member_object.is_null()) {
      print_object_details(member_object, member_name, confdb, spaces + "  ", list_of_applications);
    }
  } catch (conffwk::Exception& exc) {
    try {
      std::vector<conffwk::ConfigObject> member_object_list;
      parent_config_object.get(member_name, member_object_list);
      for (uint32_t idx = 0; idx < member_object_list.size(); ++idx) {
        if (!member_object_list[idx].is_null()) {
          if (member_name != "inputs" && member_name != "outputs") {
            std::ostringstream oss_name;
            oss_name << member_name << "[" << idx << "]";
            print_object_details(member_object_list[idx], oss_name.str(), confdb, spaces + "  ", list_of_applications);
          }
        }
      }
    } catch (conffwk::Exception& exc) {
    }
  }
}

// function for printing out the details of a specified ConfigObject
void
print_object_details(conffwk::ConfigObject& config_object_to_print,
                     const std::string& object_name,
                     conffwk::Configuration* confdb,
                     const std::string& spaces,
                     std::vector<std::string>& list_of_applications)
{
  if (object_name != "") {
    std::cout << spaces << "-----" << std::endl;
    std::cout << spaces << "\"" << object_name << "\" ";
  }
  config_object_to_print.print_ref(std::cout, *confdb, spaces);
  dunedaq::conffwk::class_t cd = confdb->get_class_info(config_object_to_print.class_name());
  for (const auto& attr : cd.p_attributes) {
    const std::string& attr_name(attr.p_name);
    print_member_details_if_needed(config_object_to_print, attr_name, confdb, spaces, list_of_applications);
    if (attr_name == "application_name") {
      std::string application_name;
      config_object_to_print.get(attr_name, application_name);
      if (application_name == "daq_application") {
        std::cout << "Application name = " << application_name << std::endl;
        std::cout << "Application UID = " << config_object_to_print.UID() << std::endl;
        list_of_applications.push_back(config_object_to_print.UID());
      }
    }
  }
  for (const auto& relship : cd.p_relationships) {
    const std::string& rel_name(relship.p_name);
    print_member_details_if_needed(config_object_to_print, rel_name, confdb, spaces, list_of_applications);
  }
}

int
main(int argc, char* argv[])
{
  if (argc < 3) {
    std::cout << "Usage: " << argv[0] << " <session> <database-file>\n";
    return 0;
  }
  logging::Logging::setup();

  std::string sessionName(argv[1]);
  std::string dbfile(argv[2]);
  conffwk::Configuration* confdb;
  std::string blah = "oksconflibs:" + dbfile;
  try {
    confdb = new conffwk::Configuration(blah);
  } catch (conffwk::Generic& exc) {
    std::cout << "Failed to load OKS database: " << exc << std::endl;
    return 0;
  }

  auto session = confdb->get<confmodel::Session>(sessionName);
  if (session == nullptr) {
    std::cout << "Failed to get Session " << sessionName << " from database\n";
    return 0;
  }

  // 14-Aug-2024, KAB: maybe this has useful information too...?
  // session->print(0, true, std::cout);
  // std::cout << "=====" << std::endl;

  std::cout << "++++++++++" << std::endl;
  std::cout << "Full-system details without generation" << std::endl;
  std::cout << "++++++++++" << std::endl;
  std::cout << std::endl;

  std::vector<std::string> list_of_application_names;
  conffwk::ConfigObject session_config_object = session->config_object();
  print_object_details(session_config_object, "", confdb, "  ", list_of_application_names);

  std::cout << std::endl;
  std::cout << "++++++++++" << std::endl;
  std::cout << "Individual application details including generation" << std::endl;
  std::cout << "++++++++++" << std::endl;

  for (size_t idx = 0; idx < list_of_application_names.size(); ++idx) {
    std::cout << std::endl;

    confdb = nullptr;
    try {
      confdb = new conffwk::Configuration(blah);
    } catch (conffwk::Generic& exc) {
      std::cout << "Failed to load OKS database: " << exc << std::endl;
      return 0;
    }
    session = confdb->get<confmodel::Session>(sessionName);

    auto daqapp = confdb->get<appmodel::SmartDaqApplication>(list_of_application_names[idx]);
    std::string appName = list_of_application_names[idx];
    if (daqapp) {
      std::cout << appName << " is of class " << daqapp->class_name() << std::endl;

      auto res = daqapp->cast<confmodel::ResourceBase>();
      if (res && res->disabled(*session)) {
        std::cout << "Application " << appName << " is disabled" << std::endl;
        continue;
      }
      std::vector<const confmodel::DaqModule*> modules;
      try {
        modules = daqapp->generate_modules(confdb, dbfile, session);
      } catch (appmodel::BadConf& exc) {
        std::cout << "Caught BadConf exception: " << exc << std::endl;
        exit(-1);
      }

      // std::cout << "Generated " << modules.size() << " modules" << std::endl;
      for (auto module : modules) {
        std::cout << "module " << module->UID() << std::endl;
        conffwk::ConfigObject module_config_object = module->config_object();
        std::vector<std::string> dummy_list;
        print_object_details(module_config_object, "", confdb, "  ", dummy_list);
        std::cout << " input objects " << std::endl;
        for (auto input : module->get_inputs()) {
          auto iObj = input->config_object();
          iObj.print_ref(std::cout, *confdb, "    ");
        }
        std::cout << " output objects " << std::endl;
        for (auto output : module->get_outputs()) {
          auto oObj = output->config_object();
          oObj.print_ref(std::cout, *confdb, "    ");
        }
      }
    } else {
      std::cout << "Failed to get SmartDaqApplication " << appName << " from database\n";
      return 0;
    }
  }
}
