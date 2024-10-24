#include "logging/Logging.hpp"

#include "conffwk/Configuration.hpp"

#include "confmodel/Component.hpp"
#include "confmodel/DaqApplication.hpp"
#include "confmodel/RCApplication.hpp"
#include "confmodel/DaqModule.hpp"
#include "confmodel/ResourceSet.hpp"
#include "confmodel/Segment.hpp"
#include "confmodel/Session.hpp"

#include "appmodel/SmartDaqApplication.hpp"

#include <iostream>
#include <string>

using namespace dunedaq;


void print_segment_application_commandline(
  const dunedaq::confmodel::Segment* segment,
  const dunedaq::confmodel::Session* session,
  conffwk::Configuration* db) {

  auto const* controller = segment->get_controller();

    std::cout << "\n" << controller->UID() << "\n";
    for (auto const& CLA: controller->construct_commandline_parameters(*db, session))
      std::cout << "CLA: " << CLA << "\n";

    for (auto const& app: segment->get_applications()) {
      std::cout << "\n" << app->UID() << "\n";
      std::vector<std::string> CLAs;
      if (app->castable("SmartDaqApplication")) {
        auto const* sdapp = db->get<dunedaq::appmodel::SmartDaqApplication>(app->UID());
        CLAs = sdapp->construct_commandline_parameters(*db, session);
      } else if (app->castable("DaqApplication")) {
        auto const* dapp = db->get<dunedaq::appmodel::SmartDaqApplication>(app->UID());
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

  if (argc < 3) {
    std::cout << "Usage: " << argv[0] << " session database-file\n";
    return 0;
  }

  std::string sessionName(argv[1]);
  dunedaq::logging::Logging::setup(sessionName, "get_apps_arguments");

  std::string confimpl = "oksconflibs:" + std::string(argv[2]);
  auto confdb = new conffwk::Configuration(confimpl);

  auto session = confdb->get<confmodel::Session>(sessionName);
  if (session==nullptr) {
    std::cerr << "Session " << sessionName << " not found in database\n";
    return -1;
  }

  print_segment_application_commandline(session->get_segment(), session, confdb);
}
