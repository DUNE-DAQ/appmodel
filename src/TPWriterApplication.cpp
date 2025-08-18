/**
 * @file TPStreamWriterApplication.cpp
 *
 * Implementation of TPStreamWriterApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */


#include "ConfigObjectFactory.hpp"
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

namespace dunedaq {
namespace appmodel {

void
TPStreamWriterApplication::generate_modules(const confmodel::Session* /*session*/) const
{
  std::vector<const conffwk::ConfigObject*> modules;

  ConfigObjectFactory obj_fac(this);


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
  conffwk::ConfigObject tset_in_net_obj = obj_fac.create_net_obj(tset_in_net_desc, ".*");


  auto source_id = get_source_id();
  if (source_id == nullptr) {
    throw(BadConf(ERS_HERE, "No SourceIDConf given to TPWriterApplication!"));  
  }

  uint tpw_idx = 0;
  std::string tpwrUid("tpwriter-"+std::to_string(source_id->get_sid()));
  conffwk::ConfigObject tpwrObj = obj_fac.create("TPStreamWriterModule", tpwrUid);
  tpwrObj.set_by_val<uint32_t>("source_id", source_id->get_sid());
  tpwrObj.set_by_val("writer_identifier", fmt::format("{}_tpw_{}", UID(), tpw_idx));
  tpwrObj.set_obj("configuration", &tpwriterConf->config_object());
  tpwrObj.set_objs("inputs", {&tset_in_net_obj} );

  modules.push_back(&obj_fac.get_dal<TPStreamWriterModule>(tpwrUid)->config_object());

  auto app_obj = obj_fac.get_dal<TPStreamWriterApplication>(UID())->config_object();
  app_obj.set_objs("modules", modules);
  configuration().update<TPStreamWriterApplication>({UID()}, {}, {});
}
 
} // namespace appmodel  
} // namespace dunedaq
