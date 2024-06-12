/**
 * @file generate_modules.cpp
 *
 * Implementation of ReadoutApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "conffwk/Configuration.hpp"
#include "appmodel/ReadoutApplication.hpp"
#include "confmodel/Session.hpp"
#include "confmodel/DetDataSender.hpp"
#include "confmodel/DetDataReceiver.hpp"
#include "confmodel/DetectorStream.hpp"


#include "appmodel/NWDetDataSender.hpp"
#include "appmodel/NWDetDataReceiver.hpp"


#include "confmodel/Connection.hpp"
#include "confmodel/GeoId.hpp"
#include "confmodel/NetworkConnection.hpp"
#include "confmodel/DetectorToDaqConnection.hpp"
#include "confmodel/ResourceSet.hpp"
#include "confmodel/Service.hpp"




#include "appmodel/DataReader.hpp"
#include "appmodel/DataReaderConf.hpp"
#include "appmodel/DataRecorder.hpp"
#include "appmodel/DataRecorderConf.hpp"

#include "appmodel/FragmentAggregator.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/RequestHandler.hpp"
#include "appmodel/ReadoutModule.hpp"
#include "appmodel/ReadoutModuleConf.hpp"

#include "appmodel/appmodelIssues.hpp"

#include "logging/Logging.hpp"
#include <fmt/core.h>

#include <string>
#include <vector>

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator __reg__("ReadoutApplication",
                                          [](const SmartDaqApplication* smartApp,
                                             conffwk::Configuration* config,
                                             const std::string& dbfile,
                                             const confmodel::Session* session) -> ModuleFactory::ReturnType {
                                            auto app = smartApp->cast<ReadoutApplication>();
                                            return app->generate_modules(config, dbfile, session);
                                          });

std::vector<const confmodel::DaqModule*>
ReadoutApplication::generate_modules(conffwk::Configuration* config,
                                     const std::string& dbfile,
                                     const confmodel::Session* session) const
{

  TLOG() << "Generating modules for application " << this->UID();


  // Retrieve configuration objects
  // Data reader
  auto reader_conf = get_data_reader();
  if (reader_conf == 0) {
    throw(BadConf(ERS_HERE, "No DataReader configuration given"));
  }

  // Link handler
  auto dlh_conf = get_link_handler();
  // What is template for?
  auto dlh_class = dlh_conf->get_template_for();


  //

  std::vector<const confmodel::DaqModule*> modules;

  // Loop over the detector to daq connections and generate one data reader per connection
  // and the cooresponding datalink handlers

  // Collect all streams    
  std::vector<const confmodel::DetectorStream*> det_streams;

  uint16_t conn_idx = 0;
  for (auto d2d_connection : get_contains()) {

    // Are we sure?
    if (d2d_connection->disabled(*session)) {
      TLOG_DEBUG(7) << "Ignoring disabled DetectorToDaqConnection " << d2d_connection->UID();
      continue;
    }

    TLOG() << "Processing DetectorToDaqConnection " << d2d_connection->UID();
    // get the readout groups and the interfaces and streams therein; 1 reaout group corresponds to 1 data reader module
    auto d2d_conn_rset = d2d_connection->cast<confmodel::DetectorToDaqConnection>();

    if (d2d_conn_rset == nullptr) {
      throw(BadConf(ERS_HERE, "ReadoutApplication contains something other than DetectorToDaqConnection"));
    }

    if (d2d_conn_rset->get_contains().empty()) {
      throw(BadConf(ERS_HERE, "DetectorToDaqConnection does not contain sebders or receivers"));
    }

    // Loop over detector 2 daq connections to find senders and receivers
    auto det_senders = d2d_conn_rset->get_senders();
    auto det_receiver = d2d_conn_rset->get_receiver();


    // Loop over senders
    for (auto s: det_senders) {
      // loop over streams
      for( auto ds : s->get_contains()) {
        det_streams.push_back(ds->cast<confmodel::DetectorStream>());
      }
    }

    // Here I want to resolve the type of connection (network, felix, or?)
    // Rules of engagement: if the receiver interface is network or felix, the receivers should be castable to the counterpart
    if ( det_receiver->cast<appmodel::NWDetDataReceiver>() != nullptr) {
      bool all_nw_senders = true;
      for( auto s : det_senders ) {
        all_nw_senders &= (s->cast<appmodel::NWDetDataSender>() != nullptr);
      }
      // Ensure that all senders are compatible with receiver
      if (!all_nw_senders) {
        throw(BadConf(ERS_HERE, "Non-network DetDataSener found with NWreceiver"));
      }
    } else {
      throw(BadConf(ERS_HERE, fmt::format("Unsupported transport type {} ({})", det_receiver->class_name(), det_receiver->UID())));
    } 

    //
    // Create data reader object
    //
    std::string reader_class = reader_conf->get_template_for();
    // Create a NIC Receiver 
    if ( reader_class != "NICReceiver ") { 
      throw(BadConf(ERS_HERE, "Unsupported receiver type"));
    }

    //
    // Instantiate a NICReceiver
    //
    // Collect all det receiver objects
    std::vector<const conffwk::ConfigObject*> det_rcvr_objs;
    for (auto d2d_connection : get_contains()) {
      det_rcvr_objs.push_back(d2d_connection.get_receiver()->config_object());
    }


    // Create the NICReceiver object
    std::string reader_uid(fmt::format("datareader-{}-{}",this->UID(),std::to_string(conn_idx++)));
    conffwk::ConfigObject reader_obj;
    TLOG() << fmt::format("creating OKS configuration object for Data reader class {} with id {}", reader_class, reader_uid);
    config->create(dbfile, reader_class, reader_uid, reader_obj);

    // Populate configuration and interfaces (leave output queues for later)
    reader_obj.set_obj("configuration", &reader_conf->config_object());
    reader_obj.set_objs("interfaces", det_rcvr_objs);

    // Prepare output queues
    // std::vector<const conffwk::ConfigObject*> qObjs;
    // for (auto q : outputQueues) {
    //   qObjs.push_back(&q->config_object());
    // }
    // reader_obj.set_objs("outputs", qObjs);

    // reader_obj.set_obj("configuration", &reader_conf->config_object());
    // reader_obj.set_objs("interfaces", if_objs);
    modules.push_back(config->get<DataReader>(reader_uid));


    //
    // Create datalink handlers
    //
    for (auto ds : det_streams) {
    //   TLOG() << "Processing stream " << ds->UID() << ", id " << ds->get_source_id() << ", det_id " << ds->get_geo_id()->get_detector_id();
      TLOG() << fmt::format("Processing stream {}, id {}, det id {}", ds->UID(), ds->get_source_id(), ds->get_geo_id()->get_detector_id());
      auto id = ds->get_source_id();
      std::string uid("DLH-" + std::to_string(id));
      conffwk::ConfigObject dlh_obj;
      TLOG() << fmt::format("creating OKS configuration object for Data Link Handler class {}, if {}", dlh_class, id);
      config->create(dbfile, dlh_class, uid, dlh_obj);
      // dlh_obj.set_by_val<uint32_t>("source_id", id);
      // dlh_obj.set_by_val<bool>("emulation_mode", emulation_mode);
      // dlh_obj.set_obj("geo_id", &stream->get_geo_id()->config_object());
      // dlh_obj.set_obj("module_configuration", &dlh_conf->config_object());
    }

  }


  return modules;
}


// std::vector<const confmodel::DaqModule*>
// ReadoutApplication::generate_modules_old(conffwk::Configuration* config,
//                                      const std::string& dbfile,
//                                      const confmodel::Session* session) const
// {
//   std::vector<const confmodel::DaqModule*> modules;

//   auto dlh_conf = get_link_handler();
//   // What is template for?
//   auto dlh_class = dlh_conf->get_template_for();

//   auto tph_conf = get_tp_handler();
//   std::string tph_class = "";
//   if (tph_conf != nullptr) {
//     tph_class = tph_conf->get_template_for();
//   }

//   // Process the queue rules looking for inputs to our DL/TP handler modules
//   const QueueDescriptor* dlh_inputq_desc = nullptr;
//   const QueueDescriptor* dlh_req_inputq_desc = nullptr;
//   const QueueDescriptor* tp_inputq_desc = nullptr;
//   // const QueueDescriptor* tpReqInputQDesc = nullptr;
//   const QueueDescriptor* fa_outputq_desc = nullptr;

//   for (auto rule : get_queue_rules()) {
//     auto destination_class = rule->get_destination_class();
//     auto data_type = rule->get_descriptor()->get_data_type();
//     if (destination_class == "ReadoutModule" || destination_class == dlh_class || destination_class == tph_class) {
//       if (data_type == "DataRequest") {
//         dlh_req_inputq_desc = rule->get_descriptor();
//       } else if (data_type == "TriggerPrimitive") {
//         tp_inputq_desc = rule->get_descriptor();
//       } else {
//         dlh_inputq_desc = rule->get_descriptor();
//       }
//     } else if (destination_class == "FragmentAggregator") {
//       fa_outputq_desc = rule->get_descriptor();
//     } else {
//       // No check on unexpected rules?
//     }
//   }

//   // Process the network rules looking for the Fragment Aggregator and TP handler data reuest inputs
//   const NetworkConnectionDescriptor* faNetDesc = nullptr;
//   const NetworkConnectionDescriptor* tpNetDesc = nullptr;
//   const NetworkConnectionDescriptor* taNetDesc = nullptr;
//   const NetworkConnectionDescriptor* tsNetDesc = nullptr;
//   for (auto rule : get_network_rules()) {
//     auto endpoint_class = rule->get_endpoint_class();
//     auto data_type = rule->get_descriptor()->get_data_type();

//     if (endpoint_class == "FragmentAggregator") {
//       faNetDesc = rule->get_descriptor();
//     }
//     else if (data_type == "TPSet") {
//         tpNetDesc = rule->get_descriptor();
//     } 
//     else if (data_type == "TriggerActivity") {
//         taNetDesc = rule->get_descriptor();
//     }
//     else if (data_type == "TimeSync") {
//         tsNetDesc = rule->get_descriptor();
//     } else {
//       // No handling of unexpected rules?
//     }
    
//   }

//   // Create here the Queue on which all data fragments are forwarded to the fragment aggregator
//   // and a container for the queues of data request to TP handler and DLH
//   if (fa_outputq_desc == nullptr) {
//     throw(BadConf(ERS_HERE, "No fragment output queue descriptor given"));
//   }
//   conffwk::ConfigObject fa_queue_obj;
//   std::vector<const confmodel::Connection*> fa_output_queues;

//   std::string fa_frag_queue_uid(fa_outputq_desc->get_uid_base());
//   config->create(dbfile, "Queue", fa_frag_queue_uid, fa_queue_obj);
//   fa_queue_obj.set_by_val<std::string>("data_type", fa_outputq_desc->get_data_type());
//   fa_queue_obj.set_by_val<std::string>("queue_type", fa_outputq_desc->get_queue_type());
//   fa_queue_obj.set_by_val<uint32_t>("capacity", fa_outputq_desc->get_capacity());

//   // Now create the TP Handler and its associated queue and network
//   // connections if we have a TP handler config
//   conffwk::ConfigObject tpReqQueueObj;
//   conffwk::ConfigObject tpQueueObj;
//   conffwk::ConfigObject tpNetObj;
//   conffwk::ConfigObject taNetObj;
//   if (tph_conf) {
//     if (tpNetDesc == nullptr) {
//       throw(BadConf(ERS_HERE, "No tpHandler network descriptor for TPSets  given"));
//     }
//     if (taNetDesc == nullptr) {
//       throw(BadConf(ERS_HERE, "No tpHandler network descriptor for TriggerActivities  given"));
//     }

//     if (tp_inputq_desc == nullptr) {
//       throw(BadConf(ERS_HERE, "No tpHandler data input queue descriptor given"));
//     }
//     if (dlh_req_inputq_desc == nullptr) {
//       throw(BadConf(ERS_HERE, "No tpHandler data request queue descriptor given"));
//     }
//     // if (tsNetDesc == nullptr) {
//     //   throw (BadConf(ERS_HERE, "No timesync output network descriptor given"));
//     // }

//     auto tpsrc = get_tp_source_id();
//     // if (tpsrc == 0) {
//     //   throw (BadConf(ERS_HERE, "No TPHandler source_id given"));
//     // }
//     std::string tpQueueUid(tp_inputq_desc->get_uid_base());
//     config->create(dbfile, "Queue", tpQueueUid, tpQueueObj);
//     tpQueueObj.set_by_val<std::string>("data_type", tp_inputq_desc->get_data_type());
//     tpQueueObj.set_by_val<std::string>("queue_type", tp_inputq_desc->get_queue_type());
//     tpQueueObj.set_by_val<uint32_t>("capacity", tp_inputq_desc->get_capacity());
//     tpQueueObj.set_by_val<uint32_t>("recv_timeout_ms", 1);
//     tpQueueObj.set_by_val<uint32_t>("send_timeout_ms", 1);

//     std::string tpReqQueueUid(dlh_req_inputq_desc->get_uid_base() + std::to_string(tpsrc));
//     config->create(dbfile, "QueueWithId", tpReqQueueUid, tpReqQueueObj);
//     tpReqQueueObj.set_by_val<std::string>("data_type", dlh_req_inputq_desc->get_data_type());
//     tpReqQueueObj.set_by_val<std::string>("queue_type", dlh_req_inputq_desc->get_queue_type());
//     tpReqQueueObj.set_by_val<uint32_t>("capacity", dlh_req_inputq_desc->get_capacity());
//     tpReqQueueObj.set_by_val<uint32_t>("source_id", tpsrc);
//     fa_output_queues.push_back(config->get<confmodel::Connection>(tpReqQueueUid));

//     auto tpServiceObj = tpNetDesc->get_associated_service()->config_object();
//     std::string tpStreamUid = tpNetDesc->get_uid_base() + UID();
//     config->create(dbfile, "NetworkConnection", tpStreamUid, tpNetObj);
//     tpNetObj.set_by_val<std::string>("data_type", tpNetDesc->get_data_type());
//     tpNetObj.set_by_val<std::string>("connection_type", tpNetDesc->get_connection_type());
//     tpNetObj.set_obj("associated_service", &tpServiceObj);

//     auto taServiceObj = taNetDesc->get_associated_service()->config_object();
//     std::string taStreamUid = taNetDesc->get_uid_base() + UID();
//     config->create(dbfile, "NetworkConnection", taStreamUid, taNetObj);
//     taNetObj.set_by_val<std::string>("data_type", taNetDesc->get_data_type());
//     taNetObj.set_by_val<std::string>("connection_type", taNetDesc->get_connection_type());
//     taNetObj.set_obj("associated_service", &taServiceObj);

//     auto tph_confObj = tph_conf->config_object();
//     conffwk::ConfigObject tpObj;
//     std::string tpUid("tphandler-" + std::to_string(tpsrc));
//     config->create(dbfile, tph_class, tpUid, tpObj);
//     tpObj.set_by_val<uint32_t>("source_id", tpsrc);
//     tpObj.set_obj("module_configuration", &tph_confObj);
//     tpObj.set_objs("inputs", { &tpQueueObj, &tpReqQueueObj });
//     tpObj.set_objs("outputs", { &tpNetObj, &taNetObj, &fa_queue_obj });

//     // Add to our list of modules to return
//     modules.push_back(config->get<ReadoutModule>(tpUid));
//   }

//   // Now create the DataReader objects, one per group of data streams
//   auto rdr_conf = get_data_reader();
//   if (rdr_conf == 0) {
//     throw(BadConf(ERS_HERE, "No DataReader configuration given"));
//   }
//   if (dlh_inputq_desc == nullptr) {
//     throw(BadConf(ERS_HERE, "No DLH data input queue descriptor given"));
//   }
//   if (dlh_req_inputq_desc == nullptr) {
//     throw(BadConf(ERS_HERE, "No DLH request input queue descriptor given"));
//   }
//   bool emulation_mode = rdr_conf->get_emulation_mode();

//   int rnum = 0;
//   // Create a DataReader for each (non-disabled) group and a Data Link
//   // Handler for each stream of this DataReader
//   for (auto d2d_connection : get_contains()) {
//     if (d2d_connection->disabled(*session)) {
//       TLOG_DEBUG(7) << "Ignoring disabled DetectorToDaqConnection " << d2d_connection->UID();
//       continue;
//     }
//     TLOG() << "Processing DetectorToDaqConnection " << d2d_connection->UID();
//     // get the readout groups and the interfaces and streams therein; 1 reaout group corresponds to 1 data reader module
//     auto d2d_conn_rset = d2d_connection->cast<confmodel::DetectorToDaqConnection>();

//     if (d2d_conn_rset == nullptr) {
//       throw(BadConf(ERS_HERE, "ReadoutApplication contains something other than DetectorToDaqConnection"));
//     }
//     std::vector<const confmodel::Connection*> outputQueues;
//     if (d2d_conn_rset->get_contains().empty()) {
//       throw(BadConf(ERS_HERE, "DetectorToDaqConnection does not contain interfaces"));
//     }

    
//     std::vector<const conffwk::ConfigObject*> if_objs;
//     // Loop over detector 2 daq connections to find senders and receivers
//     auto d2d_interfaces = d2d_conn_rset->get_contains();
//     std::vector<confmodel::DetDataSender> det_senders;
//     std::vector<confmodel::DetDataReceiver> det_receivers;

//     for ( auto d2d_if : d2d_interfaces ) {
//       auto s = d2d_if->cast<confmodel::DetDataSender>()
//       auto r = d2d_if->cast<confmodel::DetDataReceiver>()

//       // 
//       if ( s != nullptr ) {
//         det_senders.push_back(s);
//         continue;
//       } 

//       // 
//       if ( r != nullptr ) {
//         det_receivers.push_back(r);
//         continue;
//       } 
//     }

//     if (det_receivers.size() != 1) {
//         throw(BadConf(ERS_HERE, "DetectorToDaqConnection : expected 1 receiver in D2d conection {name of connection}, found {number found}"));
//     }

//     // Receiver identified
//     auto det_receiver = det_receivers.at(0);

//     // Here I want to resolve the type of connection (network, felix, or?)
//     // Rules of engagement: if the receiver interface is network or felix, the receivers should be castable to the counterpart
//     if ( det_receiver.cast<confmodel::NWDetDataReceiver>) {
//       bool all_nw_senders = true;
//       for( auto s : det_senders ) {
//         all_nw_senders &= (s.cast<>(confmodel::NWDetDataSender) != nullptr);
//       }
//       // Check if 
//       if (!all_nw_senders) {
//         throw(BadConf(ERS_HERE, "Non-network DetDataSener found with NWreceiver"));
//       }
//     } else {
//       throw(BadConf(ERS_HERE, "Unsupported receiver type"));
//     }
    
//     // Old code stars here

//     for (auto interface_rset : interfaces) {
//       if (interface_rset->disabled(*session)) {
//         TLOG_DEBUG(7) << "Ignoring disabled ReadoutInterface " << interface_rset->UID();
//         continue;
//       }
//       TLOG() << "Processing ReadoutInterface " << interface_rset->UID();
//       auto interface = interface_rset->cast<confmodel::ReadoutInterface>();
//       if (interface == nullptr) {
//         throw(BadConf(ERS_HERE, "DetectorToDaqConnection contains something othen than ReadoutInterface"));
//       }
//       if_objs.push_back(&interface->config_object());

//       for (auto res : interface->get_contains()) {
//         auto stream = res->cast<confmodel::DROStreamConf>();
//         if (stream == nullptr) {
//           throw(BadConf(ERS_HERE, "ReadoutInterface contains something other than DROStreamConf"));
//         }
//         if (stream->disabled(*session)) {
//           TLOG_DEBUG(7) << "Ignoring disabled DROStreamConf " << stream->UID();
//           continue;
//         }
//         TLOG() << "Processing stream " << stream->UID() << ", id " << stream->get_source_id() << ", det_id "
//                << stream->get_geo_id()->get_detector_id();
//         auto id = stream->get_source_id();
//         std::string uid("DLH-" + std::to_string(id));
//         conffwk::ConfigObject dlhObj;
//         TLOG_DEBUG(7) << "creating OKS configuration object for Data Link Handler class " << dlh_class << ", id " << id;
//         config->create(dbfile, dlh_class, uid, dlhObj);
//         dlhObj.set_by_val<uint32_t>("source_id", id);
//         dlhObj.set_by_val<bool>("emulation_mode", emulation_mode);
//         dlhObj.set_obj("geo_id", &stream->get_geo_id()->config_object());
//         dlhObj.set_obj("module_configuration", &dlh_conf->config_object());

//         // Time Sync network connection
//         if (dlh_conf->get_generate_timesync()) {
//           std::string tsStreamUid = tsNetDesc->get_uid_base() + std::to_string(id);
//           auto tsServiceObj = tsNetDesc->get_associated_service()->config_object();
//           conffwk::ConfigObject tsNetObj;
//           config->create(dbfile, "NetworkConnection", tsStreamUid, tsNetObj);
//           tsNetObj.set_by_val<std::string>("connection_type", tsNetDesc->get_connection_type());
//           tsNetObj.set_by_val<std::string>("data_type", tsNetDesc->get_data_type());
//           tsNetObj.set_obj("associated_service", &tsServiceObj);

//           if (tph_conf) {
//             dlhObj.set_objs("outputs", { &tpQueueObj, &fa_queue_obj, &tsNetObj });
//           } else {
//             dlhObj.set_objs("outputs", { &fa_queue_obj, &tsNetObj });
//           }
//         } else {
//           if (tph_conf) {
//             dlhObj.set_objs("outputs", { &tpQueueObj, &fa_queue_obj });
//           } else {
//             dlhObj.set_objs("outputs", { &fa_queue_obj });
//           }
//         }
//         std::string dataQueueUid(dlh_inputq_desc->get_uid_base() + std::to_string(id));
//         conffwk::ConfigObject queueObj;
//         config->create(dbfile, "QueueWithId", dataQueueUid, queueObj);
//         queueObj.set_by_val<std::string>("data_type", dlh_inputq_desc->get_data_type());
//         queueObj.set_by_val<std::string>("queue_type", dlh_inputq_desc->get_queue_type());
//         queueObj.set_by_val<uint32_t>("capacity", dlh_inputq_desc->get_capacity());
//         queueObj.set_by_val<uint32_t>("source_id", stream->get_source_id());

//         std::string reqQueueUid(dlh_req_inputq_desc->get_uid_base() + std::to_string(id));
//         conffwk::ConfigObject reqQueueObj;
//         config->create(dbfile, "QueueWithId", reqQueueUid, reqQueueObj);
//         reqQueueObj.set_by_val<std::string>("data_type", dlh_req_inputq_desc->get_data_type());
//         reqQueueObj.set_by_val<std::string>("queue_type", dlh_req_inputq_desc->get_queue_type());
//         reqQueueObj.set_by_val<uint32_t>("capacity", dlh_req_inputq_desc->get_capacity());
//         reqQueueObj.set_by_val<uint32_t>("source_id", stream->get_source_id());
//         // Add the requessts queue dal pointer to the outputs of the FragmentAggregator
//         fa_output_queues.push_back(config->get<confmodel::Connection>(reqQueueUid));

//         dlhObj.set_objs("inputs", { &queueObj, &reqQueueObj });

//         // Add the input queue dal pointer to the outputs of the DataReader
//         outputQueues.push_back(config->get<confmodel::Connection>(dataQueueUid));

//         modules.push_back(config->get<ReadoutModule>(uid));
//       }
//     }
//     std::string reader_uid("datareader-" + UID() + "-" + std::to_string(rnum++));
//     std::string readerClass = rdr_conf->get_template_for();
//     conffwk::ConfigObject readerObj;
//     TLOG_DEBUG(7) << "creating OKS configuration object for Data reader class " << readerClass;
//     config->create(dbfile, readerClass, reader_uid, readerObj);

//     std::vector<const conffwk::ConfigObject*> qObjs;
//     for (auto q : outputQueues) {
//       qObjs.push_back(&q->config_object());
//     }
//     readerObj.set_objs("outputs", qObjs);
//     readerObj.set_obj("configuration", &rdr_conf->config_object());
//     readerObj.set_objs("interfaces", if_objs);

//     modules.push_back(config->get<DataReader>(reader_uid));
//   }

//   // Finally create Fragment Aggregator
//   std::string faUid("fragmentaggregator-" + UID());
//   conffwk::ConfigObject faObj;
//   TLOG_DEBUG(7) << "creating OKS configuration object for Fragment Aggregator class ";
//   config->create(dbfile, "FragmentAggregator", faUid, faObj);

//   // Add network connection to TRBs
//   auto faServiceObj = faNetDesc->get_associated_service()->config_object();
//   std::string faNetUid = faNetDesc->get_uid_base() + UID();
//   conffwk::ConfigObject faNetObj;
//   config->create(dbfile, "NetworkConnection", faNetUid, faNetObj);
//   faNetObj.set_by_val<std::string>("connection_type", faNetDesc->get_connection_type());
//   faNetObj.set_by_val<std::string>("data_type", faNetDesc->get_data_type());
//   faNetObj.set_obj("associated_service", &faServiceObj);

//   // Add output queueus of data requests
//   std::vector<const conffwk::ConfigObject*> qObjs;
//   for (auto q : fa_output_queues) {
//     qObjs.push_back(&q->config_object());
//   }
//   faObj.set_objs("inputs", { &faNetObj, &fa_queue_obj });
//   faObj.set_objs("outputs", qObjs);

//   modules.push_back(config->get<FragmentAggregator>(faUid));

//   return modules;
// }
