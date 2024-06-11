/**
 * @file DFO.cpp
 *
 * Implementation of DFOApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "conffwk/Configuration.hpp"
#include "oks/kernel.hpp"
#include "confmodel/Connection.hpp"
#include "confmodel/NetworkConnection.hpp"
#include "appmodel/DataSubscriber.hpp"
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

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator
__reg__("HSIEventToTCApplication", [] (const SmartDaqApplication* smartApp,
                             conffwk::Configuration* confdb,
                             const std::string& dbfile,
                             const confmodel::Session* session) -> ModuleFactory::ReturnType
  {
    auto app = smartApp->cast<HSIEventToTCApplication>();
    return app->generate_modules(confdb, dbfile, session);
  }
  );

std::vector<const confmodel::DaqModule*> 
HSIEventToTCApplication::generate_modules(conffwk::Configuration* confdb,
                                     const std::string& dbfile,
                                     const confmodel::Session* /*session*/) const
{
  std::vector<const confmodel::DaqModule*> modules;


  std::string hstcUid("module-" + UID());
  conffwk::ConfigObject hstcObj;
  TLOG_DEBUG(7) << "creating OKS configuration object for the DataSubscriber class ";
  confdb->create(dbfile, "DataSubscriber", hstcUid, hstcObj);

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

    conffwk::ConfigObject connObj;
    auto serviceObj = descriptor->get_associated_service()->config_object();
    std::string connUid(descriptor->get_uid_base());
    if (descriptor->get_data_type() == "HSIEvent") {
        confdb->create(dbfile, "NetworkConnection", connUid, connObj);
        connObj.set_by_val<std::string>("data_type", descriptor->get_data_type());
        connObj.set_by_val<std::string>("connection_type", descriptor->get_connection_type());
        connObj.set_obj("associated_service", &serviceObj);

        inObj = connObj;
    } 
    else if (descriptor->get_data_type() == "TriggerCandidate") {
        confdb->create(dbfile, "NetworkConnection", connUid+UID(), connObj);
        connObj.set_by_val<std::string>("data_type", descriptor->get_data_type());
        connObj.set_by_val<std::string>("connection_type", descriptor->get_connection_type());
        connObj.set_obj("associated_service", &serviceObj);

    	outObj = connObj;
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
  modules.push_back(confdb->get<DataSubscriber>(hstcUid));

  return modules;
}
