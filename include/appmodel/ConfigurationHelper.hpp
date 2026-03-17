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

  /// Helper class to extract information from Session object without
  /// exposing the Session to user code
  ///
  /// Provides methods that loop over all applications in the session
  /// to get source IDs etc. avoiding the need for individual
  /// application's code to access configuration objects of other
  /// applications.
  class ConfigurationHelper {
  public:
    explicit ConfigurationHelper(const confmodel::Session* ses)
      : m_session(ses) {}


    /// @brief Get the exposed Services of all network connections
    /// with given data_type from all smart daq applications of given
    /// class
    ///
    /// @param app_class  Dal class of applications to match
    /// @param data_type  Data type of network descriptor to match
    ///
    /// @returns  A vector of pointers to matching Services
    std::vector<const confmodel::Service*> get_services(std::string app_class,
                                                        std::string data_type);


    /// @brief Get all NetworkConnectionDescriptors with given
    /// data_type from all applications of given type
    ///
    /// @param data_type  Data type to match in network descriptor
    /// @param app_class  Optional dal class name to match
    ///
    /// @returns a vector of application uid / network descriptor pairs
    std::vector<std::pair<std::string, const appmodel::NetworkConnectionDescriptor*>>
    get_netdescriptors (
      const std::string& data_type,
      const std::string& app_class="");


    /// @brief Get the source ids of all DetectorStreams in the Session
    ///
    /// @returns A map of application uids to vectors of streams that
    ///         they contain
    std::map<std::string, std::vector<uint32_t>> get_stream_source_ids();


    /// @brief Get the source ids of all the TP streams in all
    /// ReadoutApplications and TriggerApplications
    ///
    /// @returns  A map of application uids to vectors of contained
    ///          TP source ids
    std::map<std::string, std::vector<const SourceIDConf*>>
    get_tp_source_ids();


    /// @brief Get list of uids of applications that match given type
    ///
    /// @param app_class  Class name to select applications by. Empty
    ///                  string implies no selection by class
    ///
    /// @returns  A vector of uids of matching applications
    std::vector<std::string> get_app_uids(std::string app_class="");


    /// @brief Get list of source ids for applications that match
    ///       given type
    ///
    /// Gather the content of the SmartDaqApplication::source_id
    /// relationship for all enabled SmartDaqApplications (or those
    /// that match the given class)
    ///
    /// @param app_class  Class name to select applications by. Empty
    ///                  string implies no selection by class
    ///
    /// @returns A map of application uids to application source ids
    std::map<std::string, const SourceIDConf*>  get_app_source_ids(
      std::string app_class="");


    /// @brief Get list of all source ids for applications that match
    ///       given type. Follows any single value SourceIDConf relationship
    ///
    /// Examine all relationships of applications checking for type
    /// SourceIDConf generating a map of relationship name to
    /// SourceIDConf object pointers
    ///
    /// NB: Does not look at multi-value SourceIDConf relationships
    ///
    /// @param app_class  Class name to select applications by. Empty
    ///                  string implies no selection by class
    ///
    /// @returns  A map of application uids to maps relationship to contained
    ///          source ids
    std::map<std::string, std::map<std::string, const SourceIDConf*>>
    get_all_app_source_ids(std::string app_class="");


    /// @brief Check the enabled state of the given item
    ///
    /// @param item  The item to be checked.
    ///
    /// @returns True if the object is not disabled
    inline bool is_enabled(const conffwk::DalObject* item) {
      return !is_disabled(item);
    }

    /// @brief Check the enabled state of the given item
    ///
    /// @param item  The item to be checked.
    ///
    /// @returns True if the object is disabled
    bool is_disabled(const conffwk::DalObject* item);

  private:
    const confmodel::Session* m_session;
  };

} //namespace dunedaq::appmodel

#endif // APPMODEL_INCLUDE_CONFIGURATIONHELPER_HPP_
