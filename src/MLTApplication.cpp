/**
 * @file generate_modules.cpp
 *
 * Implementation of MLTApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "oksdbinterfaces/Configuration.hpp"

#include "coredal/Connection.hpp"
#include "coredal/NetworkConnection.hpp"
#include "coredal/ReadoutGroup.hpp"
#include "coredal/ReadoutInterface.hpp"
#include "coredal/DROStreamConf.hpp"
#include "coredal/ResourceSet.hpp"
#include "coredal/Service.hpp"
#include "coredal/Session.hpp"

#include "appdal/NetworkConnectionRule.hpp"
#include "appdal/QueueConnectionRule.hpp"

#include "appdal/QueueDescriptor.hpp"
#include "appdal/NetworkConnectionDescriptor.hpp"

#include "appdal/SourceIDConf.hpp"

#include "appdal/DataSubscriber.hpp"
#include "appdal/DataReaderConf.hpp"
#include "appdal/DataRecorderConf.hpp"

#include "appdal/ReadoutModule.hpp"
#include "appdal/ReadoutModuleConf.hpp"
#include "appdal/TCDataProcessor.hpp"

#include "appdal/ModuleLevelTrigger.hpp"
#include "appdal/ModuleLevelTriggerConf.hpp"

#include "appdal/MLTApplication.hpp"
#include "appdal/ReadoutApplication.hpp"
#include "appdal/TriggerApplication.hpp"
#include "appdal/FakeHSIApplication.hpp"
#include "appdal/appdalIssues.hpp"

#include "appdal/StandaloneCandidateMakerConf.hpp"
#include "appdal/StandaloneCandidateMaker.hpp"

#include "logging/Logging.hpp"

#include <string>
#include <vector>

using namespace dunedaq;
using namespace dunedaq::appdal;

static ModuleFactory::Registrator __reg__("MLTApplication",
                                          [](const SmartDaqApplication* smartApp,
                                             oksdbinterfaces::Configuration* confdb,
                                             const std::string& dbfile,
                                             const coredal::Session* session) -> ModuleFactory::ReturnType {
                                            auto app = smartApp->cast<MLTApplication>();
                                            return app->generate_modules(confdb, dbfile, session);
                                          });

/**
 * \brief Helper function that gets a network connection config
 *
 * \param idname Unique ID name of the config object
 * \param ntDesc Network connection descriptor object
 * \param confdb Global database configuration
 * \param dbfile Database file location
 *
 * \ret OKS configuration object for the network connection
 */
oksdbinterfaces::ConfigObject
create_mlt_network_connection(std::string uid,
                          const NetworkConnectionDescriptor* ntDesc,
                          oksdbinterfaces::Configuration* confdb,
                          const std::string& dbfile)
{
  auto ntServiceObj = ntDesc->get_associated_service()->config_object();
  oksdbinterfaces::ConfigObject ntObj;
  confdb->create(dbfile, "NetworkConnection", uid, ntObj);
  ntObj.set_by_val<std::string>("data_type", ntDesc->get_data_type());
  ntObj.set_by_val<std::string>("connection_type", ntDesc->get_connection_type());
  ntObj.set_obj("associated_service", &ntServiceObj);

  return ntObj;
}

std::vector<const coredal::DaqModule*>
MLTApplication::generate_modules(oksdbinterfaces::Configuration* confdb,
                                     const std::string& dbfile,
                                     const coredal::Session* session) const
{
  std::vector<const coredal::DaqModule*> modules;

  //auto mlt_conf = get_mlt_conf();
  //auto mlt_class = mlt_conf->get_template_for();

  auto tch_conf = get_trigger_inputs_handler();
  auto tch_class = tch_conf->get_template_for();

  auto mlt_conf = get_mlt_conf();
  auto mlt_class = mlt_conf->get_template_for();
  std::string handler_name(tch_conf->UID());

  if (!mlt_conf) {
    throw(BadConf(ERS_HERE, "No MLT configuration in MLTApplication given"));
  }

  // Queue descriptors
  // Process the queue rules looking for inputs to our trigger handler modules
  const QueueDescriptor* tc_inputq_desc = nullptr;
  const QueueDescriptor* td_outputq_desc = nullptr;

  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    auto data_type = rule->get_descriptor()->get_data_type();
    if (destination_class == tch_class) {
      tc_inputq_desc = rule->get_descriptor();
    }
    else if (destination_class == mlt_class) {
      td_outputq_desc = rule->get_descriptor();
    }
  }

  if (tc_inputq_desc == nullptr) {
      throw (BadConf(ERS_HERE, "No TC input queue descriptor given"));
  }
  if (td_outputq_desc == nullptr) {
      throw (BadConf(ERS_HERE, "No TD output-input queue descriptor given"));
  }

  // Create queues
  oksdbinterfaces::ConfigObject input_queue_obj;

  std::string queue_uid(tc_inputq_desc->get_uid_base());
  confdb->create(dbfile, "Queue", queue_uid, input_queue_obj);
  input_queue_obj.set_by_val<std::string>("data_type", tc_inputq_desc->get_data_type());
  input_queue_obj.set_by_val<std::string>("queue_type", tc_inputq_desc->get_queue_type());
  input_queue_obj.set_by_val<uint32_t>("capacity", tc_inputq_desc->get_capacity());

  oksdbinterfaces::ConfigObject output_queue_obj;

  queue_uid = td_outputq_desc->get_uid_base();
  confdb->create(dbfile, "Queue", queue_uid, output_queue_obj);
  output_queue_obj.set_by_val<std::string>("data_type", td_outputq_desc->get_data_type());
  output_queue_obj.set_by_val<std::string>("queue_type", td_outputq_desc->get_queue_type());
  output_queue_obj.set_by_val<uint32_t>("capacity", td_outputq_desc->get_capacity());


  // Net descriptors
  const NetworkConnectionDescriptor* req_net_desc = nullptr;
  const NetworkConnectionDescriptor* tc_net_desc = nullptr;
  const NetworkConnectionDescriptor* ti_net_desc = nullptr;
  const NetworkConnectionDescriptor* td_net_desc = nullptr;
  const NetworkConnectionDescriptor* timesync_net_desc = nullptr;

  for (auto rule : get_network_rules()) {
    std::string data_type = rule->get_descriptor()->get_data_type();

    // Network connections for the MLT
    if (data_type == "TriggerInhibit") {
      ti_net_desc = rule->get_descriptor();
    }
    if (data_type == "TriggerDecision") {
      td_net_desc = rule->get_descriptor();
    }
    if (data_type == "TriggerCandidate") {
      tc_net_desc = rule->get_descriptor();
    }
    if (data_type == "TimeSync") {
      timesync_net_desc  = rule->get_descriptor();
    }
    if (data_type == "DataRequest") {
      req_net_desc = rule->get_descriptor();
    }

    TLOG_DEBUG(3) << "Endpoint class (currently not used in for networkconnections): data_type: " << data_type;
  }

  if (!td_net_desc) {
    throw(BadConf(ERS_HERE, "No MLT network connection for the output TriggerDecision given"));
  }
  if (!ti_net_desc) {
    throw(BadConf(ERS_HERE, "No MLT network connection for the output TriggerInhibit given"));
  }
  if (!tc_net_desc) {
    throw(BadConf(ERS_HERE, "No MLT network connection for the Input of TriggerCandidates given"));
  }
  if (!req_net_desc) {
    throw(BadConf(ERS_HERE, "No MLT network connection for the Input of DataRequests given"));
  }
  // Network connection for input TriggerInhibit, input TCs

  oksdbinterfaces::ConfigObject ti_net_obj =
    create_mlt_network_connection(ti_net_desc->get_uid_base(), ti_net_desc, confdb, dbfile);

  oksdbinterfaces::ConfigObject tc_net_obj =
    create_mlt_network_connection(tc_net_desc->get_uid_base()+".*", tc_net_desc, confdb, dbfile);

        // Network connection for output TriggerDecision
  oksdbinterfaces::ConfigObject td_net_obj =
    create_mlt_network_connection(td_net_desc->get_uid_base(), td_net_desc, confdb, dbfile);

  // Network conection for the input Data Requests
  oksdbinterfaces::ConfigObject dr_net_obj =
    create_mlt_network_connection(req_net_desc->get_uid_base()+UID(), req_net_desc, confdb, dbfile);

  oksdbinterfaces::ConfigObject timesync_net_obj;
  if (timesync_net_desc != nullptr) {
     timesync_net_obj = create_mlt_network_connection(timesync_net_desc->get_uid_base()+".*", timesync_net_desc, confdb, dbfile); 
  }


   /**************************************************************
   * Instantiate standalone TC generator modules (e.g. random TC generator)
   **************************************************************/

   auto standalone_TC_maker_confs = get_standalone_candidate_maker_confs();
   for (auto gen_conf : standalone_TC_maker_confs) {
     oksdbinterfaces::ConfigObject gen_obj;
     confdb->create(dbfile, gen_conf->get_template_for(), gen_conf->UID(), gen_obj);
     gen_obj.set_obj("configuration", &(gen_conf->config_object()));
     if (gen_conf->get_timestamp_method() == "kTimeSync" && !timesync_net_obj.is_null()) {
	gen_obj.set_objs("inputs", {&timesync_net_obj});
     }
     gen_obj.set_objs("outputs", {&input_queue_obj});
     modules.push_back(confdb->get<StandaloneCandidateMaker>(gen_conf->UID()));
   }


  /**************************************************************
   * Create the Data Reader
   **************************************************************/
  auto rdr_conf = get_data_subscriber();
  if (rdr_conf == nullptr) {
    throw (BadConf(ERS_HERE, "No DataReader configuration given"));
  }

  std::string reader_uid("data-reader-"+UID());
  std::string reader_class = rdr_conf->get_template_for();
  oksdbinterfaces::ConfigObject reader_obj;
  TLOG_DEBUG(7) <<  "creating OKS configuration object for Data subscriber class " << reader_class;
  confdb->create(dbfile, reader_class, reader_uid, reader_obj);
  reader_obj.set_objs("inputs", {&tc_net_obj} );
  reader_obj.set_objs("outputs", {&input_queue_obj} );
  reader_obj.set_obj("configuration", &rdr_conf->config_object());

  modules.push_back(confdb->get<DataSubscriber>(reader_uid));

  /**************************************************************
   * Create the readout map 
   **************************************************************/
  
  std::vector<const dunedaq::coredal::Application*> apps = session->get_all_applications();

  std::vector<const oksdbinterfaces::ConfigObject*> sourceIds;

  for (auto app : apps) {
    auto ro_app = app->cast<appdal::ReadoutApplication>();
    if (ro_app != nullptr && !ro_app->disabled(*session)) {
  	auto resources = ro_app->get_contains();
        // Interate over all the readout groups
        for (auto roGroup : resources) {
           if (roGroup->disabled(*session)) {
              TLOG_DEBUG(7) << "Ignoring disabled ReadoutGroup " << roGroup->UID();
              continue;
        }

        auto group_rset = roGroup->cast<coredal::ReadoutGroup>();
        if (group_rset == nullptr) {
          throw(BadConf(ERS_HERE, "MLTApplication's readoutgroup list contains something other than ReadoutGroup"));
        }
        if (group_rset->get_contains().empty()) {
          throw(BadConf(ERS_HERE, "ReadoutGroup does not contain interfaces"));
        }

        // Iterate over each interface in per group
        auto interfaces = group_rset->get_contains();
        TLOG_DEBUG(7) << "Number of ReadoutInterfaces in that group : " << interfaces.size();
        for (auto interface_rset : interfaces) {
          if (interface_rset->disabled(*session)) {
            TLOG_DEBUG(7) << "Ignoring disabled ReadoutInterface " << interface_rset->UID();
            continue;
          }
          auto interface = interface_rset->cast<coredal::ReadoutInterface>();
          if (interface == nullptr) {
            throw(BadConf(ERS_HERE, "ReadoutGroup contains something othen than ReadoutInterface"));
          }
          auto streams = interface->get_contains();
          TLOG_DEBUG(7) << "Number of streams in that interface: " << streams.size();

          // Interate over all the streams
          for (auto link : streams) {
            auto stream = link->cast<coredal::DROStreamConf>();
            if (stream == nullptr) {
              throw(BadConf(ERS_HERE, "ReadoutInterface contains something other than DROStreamConf"));
            }
            if (stream->disabled(*session)) {
              TLOG_DEBUG(7) << "Ignoring disabled DROStreamConf " << stream->UID();
              continue;
            }

            // Create SourceIDConf object for the MLT
            auto id = stream->get_source_id();
            oksdbinterfaces::ConfigObject* sourceIdConf = new oksdbinterfaces::ConfigObject();
            std::string sourceIdConfUID = "dro-mlt-stream-config-" + std::to_string(id);
            confdb->create(dbfile, "SourceIDConf", sourceIdConfUID, *sourceIdConf);
            sourceIdConf->set_by_val<uint32_t>("sid", id);
            // https://github.com/DUNE-DAQ/daqdataformats/blob/5b99506675a586c8a09123900e224f2371d96df9/include/daqdataformats/detail/SourceID.hxx#L108
            sourceIdConf->set_by_val<std::string>("subsystem", "Detector_Readout");
            sourceIds.push_back(sourceIdConf);
          }
        }
      }
      if (ro_app->get_tp_source_id()!= 0) {
         oksdbinterfaces::ConfigObject* tpSourceIdConf = new oksdbinterfaces::ConfigObject();
         confdb->create(dbfile, "SourceIDConf", ro_app->UID()+"-"+ std::to_string(ro_app->get_tp_source_id()), *tpSourceIdConf);
         tpSourceIdConf->set_by_val<uint32_t>("sid", ro_app->get_tp_source_id());
         tpSourceIdConf->set_by_val<std::string>("subsystem", "Trigger");
         sourceIds.push_back(tpSourceIdConf);
      }
    }

    // SmartDaqApplication now has source_id member, might want to use that but make sure that it's actually a data source somehow...
    auto trg_app = app->cast<appdal::TriggerApplication>();
    if(trg_app != nullptr && trg_app->get_source_id() != nullptr) {
      	oksdbinterfaces::ConfigObject* tcSourceIdConf = new oksdbinterfaces::ConfigObject();
        confdb->create(dbfile, "SourceIDConf", trg_app->UID()+"-"+ std::to_string(trg_app->get_source_id()->get_sid()), *tcSourceIdConf);
        tcSourceIdConf->set_by_val<uint32_t>("sid", trg_app->get_source_id()->get_sid());
        tcSourceIdConf->set_by_val<std::string>("subsystem", trg_app->get_source_id()->get_subsystem());
        sourceIds.push_back(tcSourceIdConf);
    }
    
    // FIXME: add here same logics for HSI application(s)
    //
    auto hsi_app = app->cast<appdal::FakeHSIApplication>();
    if(hsi_app != nullptr && hsi_app->get_source_id() != nullptr) {
        oksdbinterfaces::ConfigObject* hsEventSourceIdConf = new oksdbinterfaces::ConfigObject();
        confdb->create(dbfile, "SourceIDConf", hsi_app->UID()+"-"+ std::to_string(hsi_app->get_source_id()->get_sid()), *hsEventSourceIdConf);
        hsEventSourceIdConf->set_by_val<uint32_t>("sid", hsi_app->get_source_id()->get_sid());
        hsEventSourceIdConf->set_by_val<std::string>("subsystem", hsi_app->get_source_id()->get_subsystem());
        sourceIds.push_back(hsEventSourceIdConf);
    }

  }

  // Get mandatory links
  std::vector<const oksdbinterfaces::ConfigObject*> mandatory_sids;
  const TCDataProcessor* tc_dp = tch_conf->get_data_processor()->cast<TCDataProcessor>();
  if (tc_dp != nullptr) {
	  for (auto m: tc_dp->get_mandatory_links()) {
		  mandatory_sids.push_back(&m->config_object());
	  }
  }

  /**************************************************************
   * Create the TC handler
   **************************************************************/

  auto tch_conf_obj = tch_conf->config_object();
  oksdbinterfaces::ConfigObject ti_obj;
  if (get_source_id() == nullptr) {
    throw(BadConf(ERS_HERE, "No source_id associated with this TriggerApplication!"));
  }
  uint32_t source_id = get_source_id()->get_sid();
  std::string ti_uid(handler_name + "-"+ std::to_string(source_id));
  confdb->create(dbfile, tch_class, ti_uid, ti_obj);
  ti_obj.set_by_val<uint32_t>("source_id", source_id);
  ti_obj.set_obj("module_configuration", &tch_conf_obj);
  ti_obj.set_objs("enabled_source_ids", sourceIds);
  ti_obj.set_objs("mandatory_source_ids", mandatory_sids);
  ti_obj.set_objs("inputs", {&input_queue_obj, &dr_net_obj});
  ti_obj.set_objs("outputs", {&output_queue_obj});

  // Add to our list of modules to return
   modules.push_back(confdb->get<ReadoutModule>(ti_uid));

  /**************************************************************
   * Instantiate the ModuleLevelTrigger module
   **************************************************************/

   oksdbinterfaces::ConfigObject mlt_obj;
   confdb->create(dbfile, mlt_conf->get_template_for(), mlt_conf->UID(), mlt_obj);
   mlt_obj.set_obj("configuration", &(mlt_conf->config_object()));
   mlt_obj.set_objs("inputs", {&output_queue_obj, &ti_net_obj});
   mlt_obj.set_objs("outputs", {&td_net_obj});
   modules.push_back(confdb->get<ModuleLevelTrigger>(mlt_conf->UID()));

  return modules;
}
