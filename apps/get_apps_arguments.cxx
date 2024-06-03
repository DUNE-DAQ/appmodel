#include "logging/Logging.hpp"

#include "conffwk/Configuration.hpp"

#include "coredal/Component.hpp"
#include "coredal/DaqApplication.hpp"
#include "coredal/RCApplication.hpp"
#include "coredal/DaqModule.hpp"
#include "coredal/ResourceSet.hpp"
#include "coredal/Segment.hpp"
#include "coredal/Session.hpp"

#include "appdal/SmartDaqApplication.hpp"

#include <iostream>
#include <string>

using namespace dunedaq;


void print_segment_application_commandline(
  const dunedaq::coredal::Segment* segment,
  const dunedaq::coredal::Session* session,
  conffwk::Configuration* db) {

  auto const* controller = segment->get_controller();

    std::cout << "\n" << controller->UID() << "\n";
    for (auto const& CLA: controller->construct_commandline_parameters(*db, session))
      std::cout << "CLA: " << CLA << "\n";

    for (auto const& app: segment->get_applications()) {
      std::cout << "\n" << app->UID() << "\n";
      std::vector<std::string> CLAs;
      if (app->castable("SmartDaqApplication")) {
        auto const* sdapp = db->get<dunedaq::appdal::SmartDaqApplication>(app->UID());
        CLAs = sdapp->construct_commandline_parameters(*db, session);
      } else if (app->castable("DaqApplication")) {
        auto const* dapp = db->get<dunedaq::appdal::SmartDaqApplication>(app->UID());
        CLAs = dapp->construct_commandline_parameters(*db, session);
      } else {
        CLAs = app->get_commandline_parameters();
      }

      for (auto const& CLA: CLAs)
        std::cout << "CLA: " << CLA << "\n";
    }

  for (auto const& segment: segment->get_segments())
    print_segment_application_commandline(segment, session, db);

}


int main(int argc, char* argv[]) {
  dunedaq::logging::Logging::setup();

  if (argc < 3) {
    std::cout << "Usage: " << argv[0] << " session database-file\n";
    return 0;
  }
  std::string confimpl = "oksconflibs:" + std::string(argv[2]);
  auto confdb = new conffwk::Configuration(confimpl);

  std::string sessionName(argv[1]);
  auto session = confdb->get<coredal::Session>(sessionName);
  if (session==nullptr) {
    std::cerr << "Session " << sessionName << " not found in database\n";
    return -1;
  }

  print_segment_application_commandline(session->get_segment(), session, confdb);
}
