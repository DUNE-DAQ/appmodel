/**
 * @file ConfigurationHelper.hpp
 *
 * Define a helper class for SmartDaqApplication module generators
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef APPMODEL_INCLUDE_CONFIGURATIONHELPER_HPP_
#define APPMODEL_INCLUDE_CONFIGURATIONHELPER_HPP_

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace dunedaq::confmodel {
  class Session;
  class Service;
} //namespace dunedaq::confmodel
namespace dunedaq::conffwk {
  class DalObject;
} // namespace dunedaq::conffwk

namespace dunedaq::appmodel {
  class NetworkConnectionDescriptor;
  class SourceIDConf;

  class ConfigurationHelper {
  public:
    explicit ConfigurationHelper(const confmodel::Session* ses)
      : m_session(ses) {}


    /// Get the exposed Services of all network connections with given
    /// data_type from all applications of given type 
    std::vector<const confmodel::Service*> get_services(std::string app_class,
                                                        std::string data_type);

    /// Get all NetworkConnectionDescriptors with given data_type from
    /// all applications of given type
    std::vector<std::pair<std::string, const appmodel::NetworkConnectionDescriptor*>>
    get_netdescriptors (
      const std::string& data_type,
      const std::string& app_class="");

    /// Get the source ids of all DetectorStreams in the Session
    std::map<std::string, std::vector<uint32_t>> get_stream_source_ids();

    /// Get the source ids of all the TP streams in all
    /// ReadoutApplications and TriggerApplications
    std::map<std::string, std::vector<const SourceIDConf*>>
    get_tp_source_ids();

    /// Get list of uids of applications that match given type
    std::vector<std::string> get_app_uids(std::string app_class="");

    std::map<std::string, const SourceIDConf*>  get_app_source_ids(
      std::string app_class="");

    /// Check the enabled state of the given item
    bool enabled(const conffwk::DalObject* item);

  private:
    const confmodel::Session* m_session;
  };

} //namespace dunedaq::appmodel

#endif // APPMODEL_INCLUDE_CONFIGURATIONHELPER_HPP_
