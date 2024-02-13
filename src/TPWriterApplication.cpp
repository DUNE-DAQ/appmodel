/**
 * @file DFO.cpp
 *
 * Implementation of TPStreamWriterApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "oksdbinterfaces/Configuration.hpp"
#include "oks/kernel.hpp"
#include "coredal/Connection.hpp"
#include "coredal/NetworkConnection.hpp"
#include "appdal/TPStreamWriterApplication.hpp"
#include "appdal/TPStreamWriterConf.hpp"
#include "appdal/TPStreamWriter.hpp"
#include "appdal/NetworkConnectionRule.hpp"
#include "appdal/NetworkConnectionDescriptor.hpp"
#include "appdal/QueueConnectionRule.hpp"
#include "appdal/QueueDescriptor.hpp"
#include "appdal/appdalIssues.hpp"
#include "logging/Logging.hpp"

#include <string>
#include <vector>
#include <iostream>

using namespace dunedaq;
using namespace dunedaq::appdal;

std::vector<const coredal::DaqModule*> 
TPStreamWriterApplication::generate_modules(const std::string& dbfile,
                                            const coredal::Session* session) const
{
  std::vector<const coredal::DaqModule*> modules;
  auto confdb = &configuration();

  auto tpwriterConf = get_tp_writer();
  if (tpwriterConf == 0) {
    throw (BadConf(ERS_HERE, "No TPStreamWriter configuration given"));
  }
  auto tpwriterConfObj = tpwriterConf->config_object();
  auto dataStoreParams = tpwriterConf->get_data_store_params();

  // Get TPSet sources
/*
  auto roMap = session->get_readout_map();
  for (auto roGroup : roMap->get_groups()) {
    if (roGroup->disabled(*session)) {
      TLOG() << "Ignoring disabled ReadoutGroup " << roGroup->UID();
      continue; 
    }
    auto rset = roGroup->cast<coredal::ReadoutGroup>();
    if (rset == nullptr) {
      throw (BadConf(ERS_HERE, "TPStreamWriterApplication contains something other than ReadoutGroup"));
    }
  }
*/

  //oksdbinterfaces::ConfigObject 
  std::string tpNetUid("SubToTPH-"+std::to_string(1));

  oksdbinterfaces::ConfigObject tpwrObj;
  std::string tpwrUid("tpwriter-"+std::to_string(100));
  confdb->create(dbfile, "TPStreamWriter", tpwrUid, tpwrObj);
  tpwrObj.set_obj("configuration", &tpwriterConf->config_object());
  //tpwrObj.set_obj("inputs", {&} );


  modules.push_back(confdb->get<TPStreamWriter>(tpwrUid));

  // Process the queue rules looking for inputs to our TPStreamWriter module
  const QueueDescriptor *tpwrInputQDesc = nullptr;
  for (auto rule : get_queue_rules()) {
    //auto destination_class = rule->get_destination_class();
    // if (destination_class)
    std::cout << "RULE is: " << rule << '\n'; 
  }

  

  return modules;
}
