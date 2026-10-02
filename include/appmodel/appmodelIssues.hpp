/**
 * @file appmodelIssues.hpp
 *
 * Define common ERS issues for the appmodel package
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef APPMODEL_INCLUDE_APPMODEL_APPMODELISSUES_HPP_
#define APPMODEL_INCLUDE_APPMODEL_APPMODELISSUES_HPP_

#include "ers/Issue.hpp"
#include "logging/Logging.hpp" // NOTE: if ISSUES ARE DECLARED BEFORE include logging/Logging.hpp, TLOG_DEBUG<<issue wont work.

#include <string>

namespace dunedaq {
ERS_DECLARE_ISSUE(appmodel, BadConf, what, ((std::string)what))
ERS_DECLARE_ISSUE(appmodel,
                  BadStreamConf,
                  "Failed to cast stream parameters " << id << " to " << stype,
                  ((std::string)id)((std::string)stype))

ERS_DECLARE_ISSUE(appmodel, MissingDaphne, "Daphne configuration has no board " << id, ((std::string)id))

ERS_DECLARE_ISSUE(appmodel,
                  MissingAFE,
                  "Board " << board << "uses afe " << afe << "but it's not available",
                  ((std::string)board)((std::size_t)afe))

ERS_DECLARE_ISSUE(appmodel,
                  UnimplementedMethodCalled,
                  "Method '" << method_name << "' was called but is not implemented in this class",
                  ((std::string)method_name))

ERS_DECLARE_ISSUE(appmodel, NotSmart, "Object is not a SmartDaqApplication: " << obj, ((std::string)obj))

ERS_DECLARE_ISSUE(appmodel, BadD2d, "Contained object is not a DetectorToDaqConnection: " << obj, ((std::string)obj))

} // namespace dunedaq

#endif // APPMODEL_INCLUDE_APPMODEL_APPMODELISSUES_HPP_
