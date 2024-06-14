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
#include "appmodel/DFOApplication.hpp"
#include "appmodel/DFOConf.hpp"
#include "appmodel/DFOModule.hpp"
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
__reg__("DFOApplication", [] (const SmartDaqApplication* smartApp,
                             conffwk::Configuration* confdb,
                             const std::string& dbfile,
                             const confmodel::Session* session) -> ModuleFactory::ReturnType
  {
    auto app = smartApp->cast<DFOApplication>();
    return app->generate_modules(confdb, dbfile, session);
  }
  );

std::vector<const confmodel::DaqModule*> 
DFOApplication::generate_modules(conffwk::Configuration* confdb,
                                     const std::string& dbfile,
                                     const confmodel::Session* /*session*/) const
{
  std::vector<const confmodel::DaqModule*> modules;


  std::string dfoUid("DFO-" + UID());
  conffwk::ConfigObject dfoObj;
  TLOG_DEBUG(7) << "creating OKS configuration object for DFOModule class ";
  confdb->create(dbfile, "DFOModule", dfoUid, dfoObj);

  auto dfoConf = get_dfo();
  dfoObj.set_obj("configuration", &dfoConf->config_object());

  if (dfoConf == 0) {
    throw(BadConf(ERS_HERE, "No DFOConf configuration given"));
  }

  std::vector<const conffwk::ConfigObject*> output_conns;
  std::vector<const conffwk::ConfigObject*> input_conns;
  conffwk::ConfigObject tdInObj;
  conffwk::ConfigObject busyOutObj;
  conffwk::ConfigObject tokenInObj;

  for (auto rule : get_network_rules()) {
    auto endpoint_class = rule->get_endpoint_class();
    auto descriptor = rule->get_descriptor();

    conffwk::ConfigObject connObj;
    auto serviceObj = descriptor->get_associated_service()->config_object();
    std::string connUid(descriptor->get_uid_base());
    confdb->create(dbfile, "NetworkConnection", connUid, connObj);
    connObj.set_by_val<std::string>("data_type", descriptor->get_data_type());
    connObj.set_by_val<std::string>("connection_type", descriptor->get_connection_type());
    connObj.set_obj("associated_service", &serviceObj);

    //if (endpoint_class == "DFOModule") {
    if (descriptor->get_data_type() == "TriggerDecision") {
        tdInObj = connObj;
        input_conns.push_back(&tdInObj);
      } 
    else if (descriptor->get_data_type() == "TriggerDecisionToken") {
        tokenInObj = connObj;
        input_conns.push_back(&tokenInObj);
      }
    
    else if (descriptor->get_data_type() == "TriggerInhibit") {
	busyOutObj = connObj;
        output_conns.push_back(&busyOutObj);
    }
  }

  if (tdInObj == nullptr) {
    throw(BadConf(ERS_HERE, "No TriggerDecision input connection descriptor given"));
  }
  if (busyOutObj == nullptr) {
    throw(BadConf(ERS_HERE, "No TriggerInhibit output connection descriptor given"));
  }
  if (tokenInObj == nullptr) {
    throw(BadConf(ERS_HERE, "No TriggerDecisionToken input connection descriptor given"));
  }

  dfoObj.set_objs("inputs", input_conns);
  dfoObj.set_objs("outputs", output_conns);

  // Add to our list of modules to return
  modules.push_back(confdb->get<DFOModule>(dfoUid));

  return modules;
}
