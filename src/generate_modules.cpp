/**
 * @file generate_modules .cpp
 *
 * Implementation of ReadoutApplication's dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "oksdbinterfaces/Configuration.hpp"

#include "coredal/Connection.hpp"
#include "coredal/NetworkConnection.hpp"

#include "readoutdal/DataReader.hpp"
#include "readoutdal/DataReaderConf.hpp"
#include "readoutdal/DataStreamDesccriptor.hpp"
#include "readoutdal/DLH.hpp"
#include "readoutdal/LinkHandlerConf.hpp"
#include "readoutdal/NetworkConnectionRule.hpp"
#include "readoutdal/NetworkConnectionDescriptor.hpp"
#include "readoutdal/QueueConnectionRule.hpp"
#include "readoutdal/QueueDescriptor.hpp"
#include "readoutdal/ReadoutApplication.hpp"
#include "readoutdal/TPHandler.hpp"
#include "readoutdal/TPHandlerConf.hpp"

#include "ers/Issue.hpp"
#include "logging/Logging.hpp"

#include <string>

namespace dunedaq {
  ERS_DECLARE_ISSUE(readoutdal, BadConf, what, ((std::string)what))
}

using namespace dunedaq;
using namespace dunedaq::readoutdal;

std::vector<const coredal::DaqModule*> 
ReadoutApplication::generate_modules(oksdbinterfaces::Configuration* confdb,
                                     const std::string& dbfile) const {
  std::vector<const coredal::DaqModule*> modules;
  auto linkHandler = get_link_handler();
  auto lhObj = linkHandler->config_object();

  const QueueDescriptor* inputQDesc = nullptr;
  const QueueDescriptor* outputQDesc = nullptr;
  for (auto rule : get_queue_rules()) {
    if (rule->get_destination_class() == "DLH") {
      inputQDesc = rule->get_descriptor();
    }
    else if (rule->get_destination_class() == "TPHandler") {
      outputQDesc = rule->get_descriptor();
    }
  }

  std::vector<const NetworkConnectionDescriptor*> dlhNetDesc;
  const NetworkConnectionDescriptor* tpNetDesc = nullptr;
  for (auto rule : get_network_rules()) {
    if (rule->get_endpoint_class() == "DLH") {
      dlhNetDesc.push_back(rule->get_descriptor());
    }
    else if (rule->get_endpoint_class() == "TPHandler") {
      tpNetDesc = rule->get_descriptor();
    }
  }

  oksdbinterfaces::ConfigObject tpQueueObj;
  oksdbinterfaces::ConfigObject tpNetObj;
  auto tpHandlerConf = get_tp_handler();
  if (tpHandlerConf) {
    if (tpNetDesc == nullptr) {
      throw (BadConf(ERS_HERE, "No tpHandler network descriptor given"));
    }
    if (outputQDesc == nullptr) {
      throw (BadConf(ERS_HERE, "No tpHandler input queue descriptor given"));
    }
    auto tpsrc = get_tp_src_id();
    if (tpsrc == 0) {
      throw (BadConf(ERS_HERE, "No TPHandler src_id given"));
    }
    std::string tpQueueUid("inputToTPH-"+std::to_string(tpsrc));
    confdb->create(dbfile, "Queue", tpQueueUid, tpQueueObj);
    tpQueueObj.set_by_val<std::string>("data_type", outputQDesc->get_data_type());
    tpQueueObj.set_by_val<std::string>("queue_type", outputQDesc->get_queue_type());
    tpQueueObj.set_by_val<uint32_t>("capacity", outputQDesc->get_capacity());

    std::string tpNetUid("ReqToTPH-"+std::to_string(tpsrc));
    confdb->create(dbfile, "NetworkConnection", tpNetUid, tpNetObj);
    tpNetObj.set_by_val<std::string>("data_type", tpNetDesc->get_data_type());
    tpNetObj.set_by_val<std::string>("connection_type", tpNetDesc->get_connection_type());
    tpNetObj.set_by_val<std::string>("uri", tpNetDesc->get_uri());
    
    auto tphConfObj = tpHandlerConf->config_object();
    oksdbinterfaces::ConfigObject tpObj;
    std::string tpUid("tphandler-"+std::to_string(tpsrc));
    confdb->create(dbfile, "TPHandler", tpUid, tpObj);
    tpObj.set_by_val<uint32_t>("source_id", tpsrc);
    tpObj.set_obj("handler_configuration", &tphConfObj);
    tpObj.set_objs("inputs", {&tpQueueObj, &tpNetObj});

    auto tphDal = const_cast<TPHandler*>(confdb->get<TPHandler>(tpUid));
    modules.push_back(tphDal);
  }

  auto rdrConf = get_data_reader();
  if (rdrConf == 0) {
    throw (BadConf(ERS_HERE, "No DataReader configuration given"));
  } 
  auto readerConfObj = rdrConf->config_object();
  int rnum = 0;
  for (auto stream : get_data_streams()) {
    std::vector<const coredal::Connection*> outputQueues;
    for (auto id : stream->get_src_ids()) {
      std::string uid("DLH-"+std::to_string(id));
      oksdbinterfaces::ConfigObject dlhObj;
      confdb->create(dbfile, "DLH", uid, dlhObj);
      dlhObj.set_by_val<uint32_t>("source_id", id);
      dlhObj.set_obj("handler_configuration", &lhObj);
      if (tpHandlerConf) {
        dlhObj.set_objs("outputs", {&tpQueueObj});
      }
      std::string queueUid("inputToDLH-"+std::to_string(id));
      oksdbinterfaces::ConfigObject queueObj;
      confdb->create(dbfile, "Queue", queueUid, queueObj);
      queueObj.set_by_val<std::string>("data_type", inputQDesc->get_data_type());
      queueObj.set_by_val<std::string>("queue_type", inputQDesc->get_queue_type());
      queueObj.set_by_val<uint32_t>("capacity", inputQDesc->get_capacity());

      std::vector<const oksdbinterfaces::ConfigObject*> netObjs;
      for (auto desc : dlhNetDesc) {
        std::string netUid("ReqToDLH-"+desc->get_data_type()+std::to_string(id));
        oksdbinterfaces::ConfigObject netObj;
        confdb->create(dbfile, "NetworkConnection", netUid, netObj);
        netObj.set_by_val<std::string>("data_type", desc->get_data_type());
        netObj.set_by_val<std::string>("connection_type", desc->get_connection_type());
        netObj.set_by_val<std::string>("uri", desc->get_uri());
        netObjs.push_back(&(confdb->get<coredal::NetworkConnection>(netUid)->config_object()));
      }
      dlhObj.set_objs("inputs", netObjs);

      // Add the input queue dal pointer to the outputs of the DataReader
      outputQueues.push_back(confdb->get<coredal::Connection>(queueUid));

      auto dlhDal = const_cast<DLH*>(confdb->get<DLH>(uid));
      modules.push_back(dlhDal);
    }

    std::string readerUid("datareader-"+UID()+"-"+std::to_string(rnum++));
    std::string readerClass = rdrConf->get_template_for();
    oksdbinterfaces::ConfigObject readerObj;
    TLOG_DEBUG(7) <<  "creating OKS configuration object for Data reader class " << readerClass;
    confdb->create(dbfile, readerClass, readerUid, readerObj);

    std::vector<const oksdbinterfaces::ConfigObject*> qObjs;
    for (auto q : outputQueues) {
      qObjs.push_back(&q->config_object());
    }
    readerObj.set_objs("outputs", qObjs);
    readerObj.set_obj("configuration", &readerConfObj);
    auto reader = const_cast<DataReader*>(
      confdb->get<DataReader>(readerUid));
    modules.push_back(reader);
  }
  return modules;
}
