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

#include "appdal/MLTApplication.hpp"
#include "appdal/ModuleLevelTriggerConf.hpp"
#include "appdal/ModuleLevelTrigger.hpp"

#include "appdal/ReadoutApplication.hpp"
#include "appdal/TriggerApplication.hpp"
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

  auto mlt_conf = get_mlt_conf();
  auto mlt_class = mlt_conf->get_template_for();

  if (!mlt_conf) {
    throw(BadConf(ERS_HERE, "No MLT configuration in MLTApplication given"));
  }

  // Net descriptors
  const NetworkConnectionDescriptor* tcMLTNetDesc = nullptr;
  const NetworkConnectionDescriptor* tiMLTNetDesc = nullptr;
  const NetworkConnectionDescriptor* tdMLTNetDesc = nullptr;
  const NetworkConnectionDescriptor* timesyncNetDesc = nullptr;

  /**************************************************************
   * Get all the network connections
   **************************************************************/
  for (auto rule : get_network_rules()) {
    std::string endpoint_class = rule->get_endpoint_class();
    std::string data_type = rule->get_descriptor()->get_data_type();

    // Network connections for the MLT
    if (data_type == "TriggerInhibit") {
      tiMLTNetDesc = rule->get_descriptor();
    }
    if (data_type == "TriggerDecision") {
      tdMLTNetDesc = rule->get_descriptor();
    }
    if (data_type == "TriggerCandidate") {
      tcMLTNetDesc = rule->get_descriptor();
    }
    if (data_type == "TimeSync") {
      timesyncNetDesc  = rule->get_descriptor();
    }
    TLOG_DEBUG(3) << "Endpoint class (currently not used in for networkconnections): " << endpoint_class
                  << " data_type: " << data_type;
  }

  if (!tdMLTNetDesc) {
    throw(BadConf(ERS_HERE, "No MLT network connection for the output TriggerDecision given"));
  }
  if (!tiMLTNetDesc) {
    throw(BadConf(ERS_HERE, "No MLT network connection for the output TriggerInhibit given"));
  }
  if (!tcMLTNetDesc) {
    throw(BadConf(ERS_HERE, "No MLT network connection for the Input of TriggerCandidates given"));
  }

  // Network connection for the MLT: input TriggerInhibit, input TCs

  oksdbinterfaces::ConfigObject tiMLTNetObj =
    create_mlt_network_connection(tiMLTNetDesc->get_uid_base(), tiMLTNetDesc, confdb, dbfile);

  oksdbinterfaces::ConfigObject tcMLTNetObj =
    create_mlt_network_connection(tcMLTNetDesc->get_uid_base(), tcMLTNetDesc, confdb, dbfile);

        // Network connection for the MLT: output TriggerDecision
  oksdbinterfaces::ConfigObject tdMLTNetObj =
    create_mlt_network_connection(tdMLTNetDesc->get_uid_base(), tdMLTNetDesc, confdb, dbfile);

  oksdbinterfaces::ConfigObject* timesyncNetObj = nullptr;;
  if (timesyncNetDesc != nullptr) {
     *timesyncNetObj = create_mlt_network_connection(timesyncNetDesc->get_uid_base(), timesyncNetDesc, confdb, dbfile); 
  }
   /**************************************************************
   * Instantiate standalone TC generator modules (e.g. random TC generator)
   **************************************************************/

   auto standalone_TC_maker_confs = get_standalone_candidate_maker_confs();
   for (auto gen_conf : standalone_TC_maker_confs) {
     oksdbinterfaces::ConfigObject genObj;
     confdb->create(dbfile, gen_conf->get_template_for(), gen_conf->UID(), genObj);
     genObj.set_obj("configuration", &(gen_conf->config_object()));
     genObj.set_objs("outputs", {&tcMLTNetObj});
     if (gen_conf->get_timestamp_method() == "kTimeSync" && timesyncNetObj != nullptr) {
	genObj.set_objs("inputs", {timesyncNetObj});
     }
   modules.push_back(confdb->get<StandaloneCandidateMaker>(gen_conf->UID()));
   }

  /**************************************************************
   * Create the readout map for MLT
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
            sourceIdConf->set_by_val<uint32_t>("id", id);
            // https://github.com/DUNE-DAQ/daqdataformats/blob/5b99506675a586c8a09123900e224f2371d96df9/include/daqdataformats/detail/SourceID.hxx#L108
            sourceIdConf->set_by_val<std::string>("subsystem", "Detector_Readout");
            sourceIds.push_back(sourceIdConf);
          }
        }
      }

      oksdbinterfaces::ConfigObject* tpSourceIdConf = new oksdbinterfaces::ConfigObject();
      confdb->create(dbfile, "SourceIDConf", ro_app->UID()+"-"+ std::to_string(ro_app->get_tp_source_id()), *tpSourceIdConf);
      tpSourceIdConf->set_by_val<uint32_t>("id", ro_app->get_tp_source_id());
      tpSourceIdConf->set_by_val<std::string>("subsystem", "Trigger");
      sourceIds.push_back(tpSourceIdConf);
      /*
      oksdbinterfaces::ConfigObject* taSourceIdConf = new oksdbinterfaces::ConfigObject();
      confdb->create(dbfile, "SourceIDConf", ro_app->UID()+"-"+ std::to_string(ro_app->get_ta_source_id()), *taSourceIdConf);
      taSourceIdConf->set_by_val<uint32_t>("id", ro_app->get_ta_source_id());
      taSourceIdConf->set_by_val<std::string>("subsystem", "Trigger");
      sourceIds.push_back(taSourceIdConf);
      */
    }


    auto trg_app = app->cast<appdal::TriggerApplication>();
    if(trg_app != nullptr) {
      oksdbinterfaces::ConfigObject* tcSourceIdConf = new oksdbinterfaces::ConfigObject();
      confdb->create(dbfile, "SourceIDConf", trg_app->UID()+"-"+ std::to_string(trg_app->get_source_id()), *tcSourceIdConf);
      tcSourceIdConf->set_by_val<uint32_t>("id", trg_app->get_source_id());
      tcSourceIdConf->set_by_val<std::string>("subsystem", "Trigger");
      sourceIds.push_back(tcSourceIdConf);
    }

    // FIXME: add here same logics for HSI application(s)
  }
  /**************************************************************
   * Get the MLT
   **************************************************************/

  // Create MLT config object
  auto mlt_conf_obj = mlt_conf->config_object();
  oksdbinterfaces::ConfigObject mltObj;
  std::string mltUid("mlt-" + UID());
  confdb->create(dbfile, "ModuleLevelTrigger", mltUid, mltObj);
  mltObj.set_obj("configuration", &mlt_conf_obj);
  TLOG_DEBUG(3) << "Number of mandatory readout links: " << sourceIds.size();
  mltObj.set_objs("mandatory_links", sourceIds);
  mltObj.set_objs("inputs", {&tiMLTNetObj, &tcMLTNetObj});
  mltObj.set_objs("outputs", {&tdMLTNetObj});

  modules.push_back(confdb->get<ModuleLevelTrigger>(mltUid));

  return modules;
}
