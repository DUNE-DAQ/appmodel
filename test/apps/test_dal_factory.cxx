/**
 * @file gen_readout_modules.cxx
 *
 * Quick test/demonstration of ReadoutApplication's dal method
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "logging/Logging.hpp"

#include "oksdbinterfaces/Configuration.hpp"

#include "coredal/Session.hpp"
#include "coredal/Connection.hpp"
#include "coredal/DaqModule.hpp"

#include "coredal/DaqApplication.hpp"
#include "appdal/DFApplication.hpp"
#include "appdal/DFOApplication.hpp"
#include "appdal/ReadoutApplication.hpp"
#include "appdal/SmartDaqApplication.hpp"
#include "appdal/TriggerApplication.hpp"
#include "appdal/TPStreamWriterApplication.hpp"

#include "appdal/appdalIssues.hpp"

#include "coredal/Queue.hpp"
#include "coredal/NetworkConnection.hpp"

#include <string>
using namespace dunedaq;


#include "CLI/App.hpp"
#include "CLI/Config.hpp"
#include "CLI/Formatter.hpp"



int main(int argc, char* argv[]) {

  CLI::App app{"test_dal_factory"};


  std::string dbfile;
  app.add_option("dbfile", dbfile, "Database name");  
  std::string appName;
  app.add_option("-a", appName, "Application name");
  std::string sessionName;
  app.add_option("-s", sessionName, "Session name");
  
  uint64_t num_rec(0);
  app.add_option("-n", num_rec, "Skip records");

  CLI11_PARSE(app, argc, argv);

  // if (argc < 4) {
  //   std::cout << "Usage: " << argv[0] << " <session> <smart-app> <database-file>\n";
  //   return 0;
  // }
  logging::Logging::setup();

  // std::string sessionName(argv[1]);
  // std::string appName(argv[2]);/
  // std::string dbfile(argv[3]);
  oksdbinterfaces::Configuration* confdb;
  try {
    confdb = new oksdbinterfaces::Configuration("oksconfig:" + dbfile);
  }
  catch (oksdbinterfaces::Generic& exc) {
    std::cout << "Failed to load OKS database: " << exc << std::endl;
    return 0;
  }


  // Check 
  std::cout << oksdbinterfaces::DalFactory::instance().get_known_class_name_ref(sessionName) << std::endl;

  oksdbinterfaces::ConfigObject obj;
  confdb->get("Session", sessionName, obj);


  oksdbinterfaces::DalObject* dalobj = oksdbinterfaces::DalFactory::instance().get(*confdb, obj, sessionName, false);
  std::cout << "session dal obj : " << dalobj << std::endl;
  coredal::Session* s = dynamic_cast<coredal::Session*>(dalobj);
  std::cout << "casting to  coredal::Session : " << s << std::endl;
  std::cout << "segment : " << s->get_segment() << std::endl;
  // s->print(0, false, std::cout);


  auto apps = s->get_all_applications();
  std::cout << "Found " << apps.size() << " apps" << std::endl;
  for( auto* a : apps) {
    std::cout << "app : " << a << " is_daq_app " << dynamic_cast<const coredal::DaqApplication*>(a) << std::endl;
    // a->print(0,false, std::cout);
    auto da = dynamic_cast<const coredal::DaqApplication*>(a);
    std::cout << " >> N modules : " << da->get_modules().size() << std::endl;
    for( const auto& m : da->get_modules()) {
      // m->print(4,false, std::cout);
      std::cout << ">>> " << m->UID() << std::endl;
      for( const auto& i : m->get_inputs() ){ 
        std::cout << ">>> " << i->UID()  << " " << i->get_xxx() << std::endl;
      }
    }
  }

  return 0;

}
