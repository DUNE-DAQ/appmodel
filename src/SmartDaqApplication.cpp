#include "ModuleFactory.hpp"
#include "appmodel/SmartDaqApplication.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "oks/kernel.hpp"
#include "confmodel/util.hpp"

using namespace dunedaq::appmodel;

std::vector<const dunedaq::confmodel::DaqModule*>
SmartDaqApplication::generate_modules(conffwk::Configuration* confdb,
                                      const std::string& dbfile,
                                      const confmodel::System* system) const {
  oks::OksFile::set_nolock_mode(true);
  return ModuleFactory::instance().generate(class_name(),
                                            this,
                                            confdb,
                                            dbfile,
                                            system);
}

const std::vector<std::string> SmartDaqApplication::construct_commandline_parameters(
    const conffwk::Configuration& confdb,
    const dunedaq::confmodel::System* system) const {
    return dunedaq::confmodel::construct_commandline_parameters_appfwk<dunedaq::appmodel::SmartDaqApplication>(this, confdb, system);
}
