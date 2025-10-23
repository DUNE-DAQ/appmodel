/**
 * @file DFOApplication.cpp
 *
 * Implementation of DFOApplication's generate_modules dal method
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
#include "appmodel/DataSubscriberModule.hpp"
#include "appmodel/HSIEventToTCApplication.hpp"
#include "appmodel/HSI2TCTranslatorConf.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "confmodel/Service.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "logging/Logging.hpp"

#include <string>
#include <vector>

namespace dunedaq {
namespace appmodel {

void
HSIEventToTCApplication::generate_modules(const confmodel::Session* /*session*/) const
{

  ConfigObjectFactory obj_fac(this);
  
  std::vector<const confmodel::DaqModule*> modules;

  std::string hstcUid("module-" + UID());
  TLOG_DEBUG(7) << "creating OKS configuration object for the DataSubscriberModule class ";
  conffwk::ConfigObject hstcObj = obj_fac.create("DataSubscriberModule", hstcUid);

  auto hstcConf = get_hsevent_to_tc_conf();
  hstcObj.set_obj("configuration", &hstcConf->config_object());

  if (hstcConf == 0) {
    throw(BadConf(ERS_HERE, "No HSI2TCTranslatorConf configuration given"));
  }

  conffwk::ConfigObject inObj;
  conffwk::ConfigObject outObj;

  for (auto rule : get_network_rules()) {
    auto endpoint_class = rule->get_endpoint_class();
    auto descriptor = rule->get_descriptor();

    if (descriptor->get_data_type() == "HSIEvent") {
      inObj = obj_fac.create_net_obj(descriptor, "");
    } 
    else if (descriptor->get_data_type() == "TriggerCandidate") {
      outObj = obj_fac.create_net_obj(descriptor, UID());
    }
  } 

  if (inObj == nullptr) {
    throw(BadConf(ERS_HERE, "No HSIEvent input connection descriptor given"));
  }
  if (outObj == nullptr) {
    throw(BadConf(ERS_HERE, "No TriggerCandidate output connection descriptor given"));
  }

  hstcObj.set_objs("inputs", {&inObj});
  hstcObj.set_objs("outputs", {&outObj});

  // Add to our list of modules to return
  modules.push_back(obj_fac.get_dal<DataSubscriberModule>(hstcUid));

  obj_fac.update_modules(modules);
}
 
} // namespace appmodel  
} // namespace dunedaq
