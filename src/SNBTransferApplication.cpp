/**
 * @file SNBTransferApplication.cpp
 *
 * Implementation of SNBTransferApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "appmodel/SNBTransferApplication.hpp"
#include "appmodel/SNBTransferConf.hpp"
#include "appmodel/SNBFileTransfer.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "conffwk/Configuration.hpp"
#include "confmodel/Connection.hpp"
#include "confmodel/NetworkConnection.hpp"
#include "confmodel/Service.hpp"
#include "logging/Logging.hpp"
#include "oks/kernel.hpp"

#include <string>
#include <vector>

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator __reg__("SNBTransferApplication",
                                          [](const SmartDaqApplication* smartApp,
                                             conffwk::Configuration* confdb,
                                             const std::string& dbfile,
                                             const confmodel::Session* session) -> ModuleFactory::ReturnType {
                                            auto app = smartApp->cast<SNBTransferApplication>();
                                            return app->generate_modules(confdb, dbfile, session);
                                          });

std::vector<const confmodel::DaqModule*>
SNBTransferApplication::generate_modules(conffwk::Configuration* confdb,
					 const std::string& dbfile,
					 const confmodel::Session* /*session*/) const
{
  std::vector<const confmodel::DaqModule*> modules;

  std::string snbTransferUid("snb-sample-config-" + UID());
  conffwk::ConfigObject snbTransferObj;
  TLOG_DEBUG(7) << "creating OKS configuration object for SNBTransferModule class ";
  confdb->create(dbfile, "SNBFileTransfer", snbTransferUid, snbTransferObj);

  auto snbTransferConf = get_snbt();
  snbTransferObj.set_obj("configuration", &snbTransferConf->config_object());

  if (snbTransferConf == 0) {
    throw(BadConf(ERS_HERE, "No SNBTransferConf configuration given"));
  }

  std::vector<const conffwk::ConfigObject*> input_conns;
  conffwk::ConfigObject notificationInObj;

  for (auto rule : get_network_rules()) {
    auto endpoint_class = rule->get_endpoint_class();
    auto descriptor = rule->get_descriptor();

    conffwk::ConfigObject connObj;
    auto serviceObj = descriptor->get_associated_service()->config_object();
    std::string connUid(descriptor->get_uid_base() + UID());
    confdb->create(dbfile, "NetworkConnection", connUid, connObj);
    connObj.set_by_val<std::string>("data_type", descriptor->get_data_type());
    connObj.set_by_val<std::string>("connection_type", descriptor->get_connection_type());
    connObj.set_obj("associated_service", &serviceObj);

    if (descriptor->get_data_type() == "notification_t") {
      notificationInObj = connObj;
      input_conns.push_back(&notificationInObj);
    }
  }

  if (notificationInObj == nullptr) {
    throw(BadConf(ERS_HERE, "No Notification input connection descriptor given"));
  }

  snbTransferObj.set_objs("inputs", input_conns);

  // Add to our list of modules to return
  modules.push_back(confdb->get<SNBFileTransfer>(snbTransferUid));

  return modules;
}
