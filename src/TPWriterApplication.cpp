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
#include "confmodel/NetworkConnection.hpp"
#include "appmodel/ReadoutApplication.hpp"
#include "confmodel/Service.hpp"
#include "appmodel/SourceIDConf.hpp"
#include "appmodel/TPStreamWriterApplication.hpp"
#include "appmodel/TPStreamWriterModule.hpp"
#include "appmodel/TPStreamWriterConf.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "logging/Logging.hpp"

#include <string>
#include <vector>
#include <iostream>
#include <fmt/core.h>

namespace dunedaq {
namespace appmodel {

void
TPStreamWriterApplication::generate_modules(std::shared_ptr<appmodel::ConfigurationHelper> /*helper*/) const
{
  std::vector<const confmodel::DaqModule*> modules;

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

  modules.push_back(obj_fac.get_dal<TPStreamWriterModule>(tpwrUid));

  obj_fac.update_modules(modules);
}
 
// bool TPStreamWriterApplication::compute_disabled_state(const std::set<std::string>& disabled_resources) const {
//   // Disabled if:
//   //  1. I am explicitly disabled
//   //  2. All ReadoutApplications are disabled
//   //  3. TPGeneration is disabled in all readout applications
//   TLOG()<<"Computing disabled state for "<<this->UID();

//   // First we can just check if the application itself is disabled
//   if (disabled_resources.contains(UID())) {
//     return true;
//   }

//   // Now for the tricky bit, we need to loop over the connections
//   for(auto& rule : get_network_rules()){
//     /// HACK (minor): We assume TPs will always contain this exact datatype
//     auto data_type = rule->get_descriptor()->get_data_type();
//     if (data_type != "TPSet") continue;

//     // We now loop over the parents
//     for(auto parent : configuration().referenced_by(*rule)){

//       auto readout = parent->cast<appmodel::ReadoutApplication>();
//       if(!readout){
//         continue;
//       }

//       // Check if readout is actually used by the configuration!
//       if(configuration().referenced_by(*readout).empty()){
//         TLOG()<<"NO REFERENCES";
//         continue;
//       }

//       /// If the RA is disabled then so is its TP
//       if(readout->compute_disabled_state(disabled_resources)){
//         continue;
//       }
      
//       // If the TP is enabled on ANY RA then we're enabled
//       if(readout->get_tp_generation_enabled()){
//         TLOG()<<readout->UID()<<" is enabled and has TPG enabled";
//         return false;
//       }
//     }
//   }

//   return true;
// }

bool TPStreamWriterApplication::is_disabled(const dunedaq::confmodel::ResourceTree& holder) const {
  // Disabled if:
  //  1. I am explicitly disabled
  //  2. All ReadoutApplications are disabled
  //  3. TPGeneration is disabled in all readout applications

  // First we can just check if the application itself is disabled
  if (!holder.disabled_components().is_enabled(this)){
    return true;
  }
  
  for(auto& rule : get_network_rules()){
    /// HACK (minor): We assume TPs will always contain this exact rule
    auto data_type = rule->get_descriptor()->get_data_type();
    if (data_type != "TPSet"){
      continue;
    }

    // We now loop over the parents
    for (auto parent : configuration().referenced_by(*rule, "network_rules", false, false, false, 0)) {
      // Safer than blindly casting to ReadoutApplication (RA)
      auto readout = parent->cast<appmodel::ReadoutApplication>();
      if(!readout){
        continue;
      }

      // Check if readout is actually used by the configuration!
      if(configuration().referenced_by(*readout).empty()){
        continue;
      }

      
      /// If the RA is disabled then so is its TP
      if(readout->cast<confmodel::Resource>()->is_disabled(holder)){
        continue;
      }
      // If the TP is enabled on ANY RA then we're enabled
      if(readout->get_tp_generation_enabled()){
        return false;
      }

    }
  }
  return true;
}


} // namespace appmodel  
} // namespace dunedaq
