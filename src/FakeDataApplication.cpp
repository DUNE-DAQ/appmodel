/**
 * @file generate_modules.cpp
 *
 * Implementation of FakeDataApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "ConfigObjectFactory.hpp"

#include "conffwk/Configuration.hpp"
#include "oks/kernel.hpp"

#include "confmodel/Connection.hpp"
#include "confmodel/NetworkConnection.hpp"
// #include "confmodel/ReadoutGroup.hpp"
#include "confmodel/ResourceSet.hpp"
#include "confmodel/Service.hpp"
#include "confmodel/Session.hpp"

#include "appmodel/FakeDataApplication.hpp"
#include "appmodel/FakeDataProdConf.hpp"
#include "appmodel/FakeDataProdModule.hpp"
#include "appmodel/FragmentAggregatorModule.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"

#include "appmodel/appmodelIssues.hpp"

#include "logging/Logging.hpp"

#include <string>
#include <vector>

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator __reg__("FakeDataApplication",
                                          [](const SmartDaqApplication* smartApp,
                                             conffwk::Configuration* confdb,
                                             const std::string& dbfile,
                                             const confmodel::Session* session) -> ModuleFactory::ReturnType {
                                            auto app = smartApp->cast<FakeDataApplication>();
                                            return app->generate_modules(confdb, dbfile, session);
                                          });

std::vector<const confmodel::DaqModule*>
FakeDataApplication::generate_modules(conffwk::Configuration* confdb,
                                      const std::string& dbfile,
                                      const confmodel::Session* session) const
{
  // oks::OksFile::set_nolock_mode(true);

  std::vector<const confmodel::DaqModule*> modules;

  const auto obj_fac = ConfigObjectFactory(confdb, dbfile, UID());

  // Process the queue rules looking for inputs to our DL/TP handler modules
  const QueueDescriptor* dlhReqInputQDesc = nullptr;
  const QueueDescriptor* faOutputQDesc = nullptr;

  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    auto data_type = rule->get_descriptor()->get_data_type();
    if (destination_class == "FakeDataProdModule") {
      if (data_type == "DataRequest") {
        dlhReqInputQDesc = rule->get_descriptor();
      }
    } else if (destination_class == "FragmentAggregatorModule") {
      faOutputQDesc = rule->get_descriptor();
    }
  }
  if (faOutputQDesc == nullptr) {
    throw(BadConf(ERS_HERE, "No fragment output queue descriptor given"));
  }
  if (dlhReqInputQDesc == nullptr) {
    throw(BadConf(ERS_HERE, "No DLH request input queue descriptor given"));
  }
  // Process the network rules looking for the Fragment Aggregator and TP handler data reuest inputs
  const NetworkConnectionDescriptor* faNetDesc = nullptr;
  const NetworkConnectionDescriptor* tsNetDesc = nullptr;
  for (auto rule : get_network_rules()) {
    auto endpoint_class = rule->get_endpoint_class();
    if (endpoint_class == "FragmentAggregatorModule") {
      faNetDesc = rule->get_descriptor();
    } else if (endpoint_class == "FakeDataProdModule") {
      tsNetDesc = rule->get_descriptor();
    }
  }
  if (faNetDesc == nullptr) {
    throw(BadConf(ERS_HERE, "No Fragment output network descriptor given"));
  }
  if (tsNetDesc == nullptr) {
    throw(BadConf(ERS_HERE, "No TimeSync output network descriptor given"));
  }

  // Create here the Queue on which all data fragments are forwarded to the fragment aggregator
  // and a container for the queues of data request to TP handler and DLH

  std::vector<const confmodel::Connection*> faOutputQueues;

  conffwk::ConfigObject faQueueObj = obj_fac.create_queue_obj(faOutputQDesc, UID());

  // Create a FakeDataProdModule for each stream of this Readout Group
  for (auto fdpConf : get_contains()) {
    if (fdpConf->disabled(*session)) {
      TLOG_DEBUG(7) << "Ignoring disabled FakeDataProdConf " << fdpConf->UID();
      continue;
    }

    auto stream = fdpConf->cast<appmodel::FakeDataProdConf>();
    if (stream == nullptr) {
      throw(BadConf(ERS_HERE, "ReadoutGroup contains something other than FakeDataProdConf"));
    }

    auto id = stream->get_source_id();
    std::string uid("FakeDataProdModule-" + std::to_string(id));
    TLOG_DEBUG(7) << "creating OKS configuration object for FakeDataProdModule";
    conffwk::ConfigObject dlhObj = obj_fac.create("FakeDataProdModule", uid);
    dlhObj.set_obj("configuration", &stream->config_object());

    // Time Sync network connection
    auto tsNetObj = obj_fac.create_net_obj(tsNetDesc, std::to_string(id));

    dlhObj.set_objs("outputs", { &faQueueObj, &tsNetObj });

    auto reqQueueObj = obj_fac.create_queue_sid_obj(dlhReqInputQDesc, id);

    // Add the requessts queue dal pointer to the outputs of the FragmentAggregatorModule
    faOutputQueues.push_back(confdb->get<confmodel::Connection>(
                               dlhReqInputQDesc->get_uid_base() + std::to_string(id)));

    dlhObj.set_objs("inputs", { &reqQueueObj });

    modules.push_back(confdb->get<FakeDataProdModule>(uid));
  }

  // Finally create Fragment Aggregator
  std::string faUid("fragmentaggregator-" + UID());
  TLOG_DEBUG(7) << "creating OKS configuration object for Fragment Aggregator class ";
  conffwk::ConfigObject faObj = obj_fac.create("FragmentAggregatorModule", faUid);

  // Add network connection to TRBs
  conffwk::ConfigObject faNetObj = obj_fac.create_net_obj(faNetDesc, UID());

  // Add output queueus of data requests
  std::vector<const conffwk::ConfigObject*> qObjs;
  for (auto q : faOutputQueues) {
    qObjs.push_back(&q->config_object());
  }
  faObj.set_objs("inputs", { &faNetObj, &faQueueObj });
  faObj.set_objs("outputs", qObjs);

  modules.push_back(confdb->get<FragmentAggregatorModule>(faUid));

  // oks::OksFile::set_nolock_mode(false);
  return modules;
}
