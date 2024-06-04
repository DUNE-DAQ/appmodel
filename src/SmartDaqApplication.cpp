#include "ModuleFactory.hpp"
#include "appdal/SmartDaqApplication.hpp"
#include "appdal/appdalIssues.hpp"
#include "oks/kernel.hpp"
#include "coredal/util.hpp"

using namespace dunedaq::appdal;

std::vector<const dunedaq::coredal::DaqModule*>
SmartDaqApplication::generate_modules(conffwk::Configuration* confdb,
                                      const std::string& dbfile,
                                      const coredal::Session* session) const {
  oks::OksFile::set_nolock_mode(true);
  return ModuleFactory::instance().generate(class_name(),
                                            this,
                                            confdb,
                                            dbfile,
                                            session);
}

const std::vector<std::string> SmartDaqApplication::construct_commandline_parameters(
    const conffwk::Configuration& confdb,
    const dunedaq::coredal::Session* session) const {
    return dunedaq::coredal::construct_commandline_parameters_appfwk<dunedaq::appdal::SmartDaqApplication>(this, confdb, session);
}
