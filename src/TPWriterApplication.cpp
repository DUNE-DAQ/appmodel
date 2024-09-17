/**
 * @file DFO.cpp
 *
 * Implementation of TPStreamWriterApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "conffwk/Configuration.hpp"
#include "oks/kernel.hpp"
#include "confmodel/Connection.hpp"
#include "confmodel/Service.hpp"
#include "confmodel/NetworkConnection.hpp"
#include "appmodel/TPStreamWriterApplication.hpp"
#include "appmodel/TPStreamWriterModule.hpp"
#include "appmodel/TPStreamWriterConf.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/ReadoutApplication.hpp"
#include "appmodel/SourceIDConf.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "logging/Logging.hpp"

#include <string>
#include <vector>
#include <iostream>
#include <fmt/core.h>

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator
__reg__("TPStreamWriterApplication", [] (const SmartDaqApplication* smartApp,
                             conffwk::Configuration* confdb,
                             const std::string& dbfile,
                             const confmodel::Session* session) -> ModuleFactory::ReturnType
  {
    auto app = smartApp->cast<TPStreamWriterApplication>();
    return app->generate_modules(confdb, dbfile, session);
  }
  );

std::vector<const confmodel::DaqModule*> 
TPStreamWriterApplication::generate_modules(conffwk::Configuration* confdb,
                                            const std::string& dbfile,
                                            const confmodel::Session* /*session*/) const
{
  std::vector<const confmodel::DaqModule*> modules;

  auto tpwriterConf = get_tp_writer();
  if (tpwriterConf == 0) {
    throw (BadConf(ERS_HERE, "No TPStreamWriterModule configuration given"));
  }
  auto tpwriterConfObj = tpwriterConf->config_object();

  const NetworkConnectionDescriptor* tset_in_net_desc = nullptr;
  for (auto rule : get_network_rules()) {
    auto endpoint_class = rule->get_endpoint_class();
    auto data_type = rule->get_descriptor()->get_data_type();
    if (data_type == "TPSet") {
      tset_in_net_desc = rule->get_descriptor();
    }
  }
  if ( tset_in_net_desc== nullptr) {
      throw (BadConf(ERS_HERE, "No network descriptor given to receive TPSets"));
  }
  // Create Network Connection
  conffwk::ConfigObject tset_in_net_obj;
  auto tset_in_service_obj = tset_in_net_desc->get_associated_service()->config_object();
  std::string tset_stream_uid(tset_in_net_desc->get_uid_base()+".*");
  confdb->create(dbfile, "NetworkConnection", tset_stream_uid, tset_in_net_obj);
  tset_in_net_obj.set_by_val<std::string>("data_type", tset_in_net_desc->get_data_type());
  tset_in_net_obj.set_by_val<std::string>("connection_type", tset_in_net_desc->get_connection_type());
  tset_in_net_obj.set_obj("associated_service", &tset_in_service_obj);

  conffwk::ConfigObject tpwrObj;

  auto source_id = get_source_id();
  if (source_id == nullptr) {
    throw(BadConf(ERS_HERE, "No SourceIDConf given to TPWriterApplication!"));  
  }

  std::string tpwrUid("tpwriter-"+std::to_string(source_id->get_sid()));
  confdb->create(dbfile, "TPStreamWriterModule", tpwrUid, tpwrObj);
  tpwrObj.set_by_val<uint32_t>("source_id", source_id->get_sid());
  tpwrObj.set_by_val("writer_identifier", fmt::format("{}_tpw_{}", UID(), source_id->get_sid()));
  tpwrObj.set_obj("configuration", &tpwriterConf->config_object());
  tpwrObj.set_objs("inputs", {&tset_in_net_obj} );

  modules.push_back(confdb->get<TPStreamWriterModule>(tpwrUid));

  return modules;
}
