/**
 * @file FakeDFOTestApplication.cpp
 *
 * Implementation of FakeDFOTestApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "appmodel/FakeDFOTestApplication.hpp"
#include "appmodel/DFOBrokerModule.hpp"
#include "appmodel/DFOBrokerConf.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/FakeDFOClientModule.hpp"
#include "appmodel/FakeDFOClientConf.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "confmodel/Connection.hpp"
#include "confmodel/NetworkConnection.hpp"
#include "confmodel/Service.hpp"
#include "logging/Logging.hpp"
#include "oks/kernel.hpp"
#include "conffwk/Configuration.hpp"

#include <string>
#include <vector>
#include <fmt/core.h>

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator __reg__("FakeDFOTestApplication",
                                          [](const SmartDaqApplication* smartApp,
                                             conffwk::Configuration* confdb,
                                             const std::string& dbfile,
                                             const confmodel::Session* session) -> ModuleFactory::ReturnType {
                                            auto app = smartApp->cast<FakeDFOTestApplication>();
                                            return app->generate_modules(confdb, dbfile, session);
                                          });

inline void
fill_queue_object_from_desc(const QueueDescriptor* qDesc, conffwk::ConfigObject& qObj)
{
  qObj.set_by_val<std::string>("data_type", qDesc->get_data_type());
  qObj.set_by_val<std::string>("queue_type", qDesc->get_queue_type());
  qObj.set_by_val<uint32_t>("capacity", qDesc->get_capacity());
}

inline void
fill_netconn_object_from_desc(const NetworkConnectionDescriptor* netDesc, conffwk::ConfigObject& netObj)
{
  netObj.set_by_val<std::string>("data_type", netDesc->get_data_type());
  netObj.set_by_val<std::string>("connection_type", netDesc->get_connection_type());

  auto serviceObj = netDesc->get_associated_service()->config_object();
  netObj.set_obj("associated_service", &serviceObj);
}

std::vector<const confmodel::DaqModule*>
FakeDFOTestApplication::generate_modules(conffwk::Configuration* confdb,
                                const std::string& dbfile,
                                const confmodel::Session* session) const
{
  std::vector<const confmodel::DaqModule*> modules;

  // Containers for module specific config objects for output/input
  // Prepare TRB output objects
  std::vector<const conffwk::ConfigObject*> trbOutputObjs;

  // -- First, we process expected Queue and Network connections and create their objects.

  // Process the queue rules looking for the TD queue and Token Queue
  const QueueDescriptor* tokenQDesc = nullptr;
  const QueueDescriptor* tdQDesc = nullptr;
  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();

    if (destination_class == "DFOBrokerModule") {
      tokenQDesc = rule->get_descriptor();
    }
    if (destination_class == "FakeDFOClientModule") {
      tdQDesc = rule->get_descriptor();
    }
  }
  if (tokenQDesc == nullptr) { // BadConf if no descriptor between DataWriterModule and DFOBroker
    throw(BadConf(ERS_HERE, "Could not find queue descriptor rule for Dataflow Tokens!"));
  }
  if (tdQDesc == nullptr) { // BadConf if no descriptor between DFOBroker and TRB
    throw(BadConf(ERS_HERE, "Could not find queue descriptor rule for TriggerDecisions!"));
  }
  // Create queue connection config object
  conffwk::ConfigObject tokenQueueObj;
  conffwk::ConfigObject tdQueueObj;
  std::string tokenQueueUid(tokenQDesc->get_uid_base() + UID());
  std::string tdQueueUid(tdQDesc->get_uid_base() + UID());
  confdb->create(dbfile, "Queue", tokenQueueUid, tokenQueueObj);
  confdb->create(dbfile, "Queue", tdQueueUid, tdQueueObj);
  fill_queue_object_from_desc(tokenQDesc, tokenQueueObj);
  fill_queue_object_from_desc(tdQDesc, tdQueueObj);

  // Process the network rules
  const NetworkConnectionDescriptor* dfodecNetDesc = nullptr;
  const NetworkConnectionDescriptor* hbNetDesc = nullptr;
  for (auto rule : get_network_rules()) {
    auto descriptor = rule->get_descriptor();
    auto data_type = descriptor->get_data_type();
    if (data_type == "DFODecision") {
      dfodecNetDesc = rule->get_descriptor();
    } else if (data_type == "DataflowHeartbeat") {
      hbNetDesc = rule->get_descriptor();
    }
  }
  if (dfodecNetDesc == nullptr) { // BadCond if no descriptor for DFODecisions into DFOBroker
    throw(BadConf(ERS_HERE, "Could not find network descriptor rule for input DFODecisions!"));
  }
  if (hbNetDesc == nullptr) { // BadCond if no descriptor for DataflowHeartbeats out of DFOBroker
    throw(BadConf(ERS_HERE, "Could not find network descriptor rule for output DataflowHeartbeats!"));
  }
  // Create network connection config object
  conffwk::ConfigObject dfodecNetObj;
  conffwk::ConfigObject hbNetObj;
  std::string dfodecNetUid = dfodecNetDesc->get_uid_base() + UID();
  std::string hbNetUid = hbNetDesc->get_uid_base();
  confdb->create(dbfile, "NetworkConnection", dfodecNetUid, dfodecNetObj);
  confdb->create(dbfile, "NetworkConnection", hbNetUid, hbNetObj);
  fill_netconn_object_from_desc(dfodecNetDesc, dfodecNetObj);
  fill_netconn_object_from_desc(hbNetDesc, hbNetObj);

  // -- Second, we create the Module objects and assign their configs, with the precreated
  // -- connection config objects above.

  auto brokerConf = get_broker();
  if (brokerConf == nullptr) {
    throw(BadConf(ERS_HERE, "No DFOBroker configuration given"));
  }
  auto brokerConfObj = brokerConf->config_object();

  conffwk::ConfigObject dfobrokerObj;
  std::string dfobUid(UID() + "-dfobroker");
  confdb->create(dbfile, "DFOBrokerModule", dfobUid, dfobrokerObj);
  dfobrokerObj.set_obj("configuration", &brokerConfObj);
  dfobrokerObj.set_objs("inputs", { &dfodecNetObj, &tokenQueueObj });
  dfobrokerObj.set_objs("outputs", { &hbNetObj, &tdQueueObj });
  modules.push_back(confdb->get<DFOBrokerModule>(dfobUid));

  auto fakedfoclient = get_dfoclient();
  if (fakedfoclient == nullptr) {
    throw(BadConf(ERS_HERE, "No FakeDFOClient configuration given"));
  }
  auto fakedfoConfObj = fakedfoclient->config_object();

  conffwk::ConfigObject fakedfoclientObj;
  std::string fakedfoclientUid(UID() + "-fakedfoclient");
  confdb->create(dbfile, "FakeDFOClientModule", fakedfoclientUid, fakedfoclientObj);
  fakedfoclientObj.set_obj("configuration", &fakedfoConfObj);
  fakedfoclientObj.set_objs("inputs", { &tdQueueObj });
  fakedfoclientObj.set_objs("outputs", { &tokenQueueObj });
  modules.push_back(confdb->get<FakeDFOClientModule>(fakedfoclientUid));

  return modules;
}
