#include "appmodel/SmartDaqApplication.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "oks/kernel.hpp"
#include "confmodel/util.hpp"

namespace dunedaq {
namespace appmodel {

const std::vector<std::string> SmartDaqApplication::construct_commandline_parameters(
    const conffwk::Configuration& confdb,
    const dunedaq::confmodel::Session* session) const {
    return dunedaq::confmodel::construct_commandline_parameters_appfwk<dunedaq::appmodel::SmartDaqApplication>(this, confdb, session);
}
 
} // namespace appmodel  
} // namespace dunedaq
