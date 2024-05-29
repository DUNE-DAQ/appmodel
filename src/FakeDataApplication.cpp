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

#include "oks/kernel.hpp"
#include "conffwk/Configuration.hpp"

#include "coredal/Connection.hpp"
#include "coredal/NetworkConnection.hpp"
#include "coredal/ReadoutGroup.hpp"
#include "coredal/ResourceSet.hpp"
#include "coredal/Service.hpp"
#include "coredal/Session.hpp"

#include "appdal/FakeDataApplication.hpp"
#include "appdal/FakeDataProd.hpp"
#include "appdal/FakeDataProdConf.hpp"
#include "appdal/FragmentAggregator.hpp"
#include "appdal/NetworkConnectionDescriptor.hpp"
#include "appdal/NetworkConnectionRule.hpp"
#include "appdal/QueueConnectionRule.hpp"
#include "appdal/QueueDescriptor.hpp"

#include "appdal/appdalIssues.hpp"

#include "logging/Logging.hpp"

#include <string>
#include <vector>

using namespace dunedaq;
using namespace dunedaq::appdal;

static ModuleFactory::Registrator __reg__("FakeDataApplication",
                                          [](const SmartDaqApplication* smartApp,
                                             conffwk::Configuration* confdb,
                                             const std::string& dbfile,
                                             const coredal::Session* session) -> ModuleFactory::ReturnType {
                                            auto app = smartApp->cast<FakeDataApplication>();
                                            return app->generate_modules(confdb, dbfile, session);
                                          });

std::vector<const coredal::DaqModule*>
FakeDataApplication::generate_modules(conffwk::Configuration* confdb,
                                      const std::string& dbfile,
                                      const coredal::Session* session) const
{
  // oks::OksFile::set_nolock_mode(true);

  std::vector<const coredal::DaqModule*> modules;

  // Process the queue rules looking for inputs to our DL/TP handler modules
  const QueueDescriptor* dlhReqInputQDesc = nullptr;
  const QueueDescriptor* faOutputQDesc = nullptr;

  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    auto data_type = rule->get_descriptor()->get_data_type();
    if (destination_class == "FakeDataProd") {
      if (data_type == "DataRequest") {
        dlhReqInputQDesc = rule->get_descriptor();
      }
    } else if (destination_class == "FragmentAggregator") {
      faOutputQDesc = rule->get_descriptor();
    }
  }
  // Process the network rules looking for the Fragment Aggregator and TP handler data reuest inputs
  const NetworkConnectionDescriptor* faNetDesc = nullptr;
  const NetworkConnectionDescriptor* tsNetDesc = nullptr;
  for (auto rule : get_network_rules()) {
    auto endpoint_class = rule->get_endpoint_class();
    if (endpoint_class == "FragmentAggregator") {
      faNetDesc = rule->get_descriptor();
    } else if (endpoint_class == "FakeDataProd") {
      tsNetDesc = rule->get_descriptor();
    }
  }

  // Create here the Queue on which all data fragments are forwarded to the fragment aggregator
  // and a container for the queues of data request to TP handler and DLH
  if (faOutputQDesc == nullptr) {
    throw(BadConf(ERS_HERE, "No fragment output queue descriptor given"));
  }
  conffwk::ConfigObject faQueueObj;
  std::vector<const coredal::Connection*> faOutputQueues;

  std::string taFragQueueUid(faOutputQDesc->get_uid_base() + UID());
  confdb->create(dbfile, "Queue", taFragQueueUid, faQueueObj);
  faQueueObj.set_by_val<std::string>("data_type", faOutputQDesc->get_data_type());
  faQueueObj.set_by_val<std::string>("queue_type", faOutputQDesc->get_queue_type());
  faQueueObj.set_by_val<uint32_t>("capacity", faOutputQDesc->get_capacity());

  if (dlhReqInputQDesc == nullptr) {
    throw(BadConf(ERS_HERE, "No DLH request input queue descriptor given"));
  }

  // Create a FakeDataProd for each stream of this Readout Group
  // for (auto roGroup : get_readout_groups()) {
  for (auto roGroup : get_contains()) {
    if (roGroup->disabled(*session)) {
      TLOG_DEBUG(7) << "Ignoring disabled ReadoutGroup " << roGroup->UID();
      continue;
    }
    auto rset = roGroup->cast<coredal::ReadoutGroup>();
    if (rset == nullptr) {
      throw(BadConf(ERS_HERE, "FakeDataApplication contains something other than ReadoutGroup"));
    }
    std::vector<const coredal::Connection*> outputQueues;
    for (auto res : rset->get_contains()) {
      auto stream = res->cast<appdal::FakeDataProdConf>();
      if (stream == nullptr) {
        throw(BadConf(ERS_HERE, "ReadoutGroup contains something other than FakeDataProdConf"));
      }
      if (stream->disabled(*session)) {
        TLOG_DEBUG(7) << "Ignoring disabled FakeDataProdConf " << stream->UID();
        continue;
      }
      auto id = stream->get_source_id();
      std::string uid("FakeDataProd-" + std::to_string(id));
      conffwk::ConfigObject dlhObj;
      TLOG_DEBUG(7) << "creating OKS configuration object for FakeDataProd";
      confdb->create(dbfile, "FakeDataProd", uid, dlhObj);
      dlhObj.set_obj("configuration", &stream->config_object());

      // Time Sync network connection
      std::string tsStreamUid = tsNetDesc->get_uid_base() + std::to_string(id);
      auto tsServiceObj = tsNetDesc->get_associated_service()->config_object();
      conffwk::ConfigObject tsNetObj;
      confdb->create(dbfile, "NetworkConnection", tsStreamUid, tsNetObj);
      tsNetObj.set_by_val<std::string>("connection_type", tsNetDesc->get_connection_type());
      tsNetObj.set_by_val<std::string>("data_type", tsNetDesc->get_data_type());
      tsNetObj.set_obj("associated_service", &tsServiceObj);

      dlhObj.set_objs("outputs", { &faQueueObj, &tsNetObj });

      std::string reqQueueUid(dlhReqInputQDesc->get_uid_base() + std::to_string(id));
      conffwk::ConfigObject reqQueueObj;
      confdb->create(dbfile, "QueueWithId", reqQueueUid, reqQueueObj);
      reqQueueObj.set_by_val<std::string>("data_type", dlhReqInputQDesc->get_data_type());
      reqQueueObj.set_by_val<std::string>("queue_type", dlhReqInputQDesc->get_queue_type());
      reqQueueObj.set_by_val<uint32_t>("capacity", dlhReqInputQDesc->get_capacity());
      reqQueueObj.set_by_val<uint32_t>("source_id", stream->get_source_id());
      // Add the requessts queue dal pointer to the outputs of the FragmentAggregator
      faOutputQueues.push_back(confdb->get<coredal::Connection>(reqQueueUid));

      dlhObj.set_objs("inputs", { &reqQueueObj });

      modules.push_back(confdb->get<FakeDataProd>(uid));
    }
  }

  // Finally create Fragment Aggregator
  std::string faUid("fragmentaggregator-" + UID());
  conffwk::ConfigObject faObj;
  TLOG_DEBUG(7) << "creating OKS configuration object for Fragment Aggregator class ";
  confdb->create(dbfile, "FragmentAggregator", faUid, faObj);

  // Add network connection to TRBs
  auto faServiceObj = faNetDesc->get_associated_service()->config_object();
  std::string faNetUid = faNetDesc->get_uid_base() + UID();
  conffwk::ConfigObject faNetObj;
  confdb->create(dbfile, "NetworkConnection", faNetUid, faNetObj);
  faNetObj.set_by_val<std::string>("connection_type", faNetDesc->get_connection_type());
  faNetObj.set_by_val<std::string>("data_type", faNetDesc->get_data_type());
  faNetObj.set_obj("associated_service", &faServiceObj);

  // Add output queueus of data requests
  std::vector<const conffwk::ConfigObject*> qObjs;
  for (auto q : faOutputQueues) {
    qObjs.push_back(&q->config_object());
  }
  faObj.set_objs("inputs", { &faNetObj, &faQueueObj });
  faObj.set_objs("outputs", qObjs);

  modules.push_back(confdb->get<FragmentAggregator>(faUid));

  // oks::OksFile::set_nolock_mode(false);
  return modules;
}
