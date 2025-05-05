/**
 * @file ConfigObjectFactory.hpp
 *
 * Define a helper class for SmartDaqApplication module generators
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef APPMODEL_INCLUDE_OBJECTFACTORY_HPP_
#define APPMODEL_INCLUDE_OBJECTFACTORY_HPP_

#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/QueueDescriptor.hpp"

#include "conffwk/Configuration.hpp"

#include "confmodel/DetectorStream.hpp"
#include "confmodel/Service.hpp"
#include "oks/file.hpp"

#include <fmt/core.h>  // Replace with std::format when we switch to a newer compiler?

namespace dunedaq::appmodel {

class ConfigObjectFactory {

  conffwk::Configuration* m_config;
  std::string m_dbfile;
  std::string m_app_uid;

  public:
  ConfigObjectFactory(conffwk::Configuration* config,
                const std::string& dbfile,
                const std::string& app_uid)
    : m_config(config),
      m_dbfile(dbfile),
      m_app_uid(app_uid) {
      
      //FIXME: remove this hacky hack
      oks::OksFile::set_nolock_mode(true);
  }

  ~ConfigObjectFactory() {
    //FIXME: remove this hacky hack
    oks::OksFile::set_nolock_mode(false);
  }

  [[nodiscard]] conffwk::ConfigObject create(const std::string& class_name,
                                             const std::string& id) const {
    conffwk::ConfigObject cfg_obj;
    m_config->create(m_dbfile, class_name, id, cfg_obj);
    // m_config->create(m_dbfile, class_name, fmt::format("{}_{}", m_app_uid, id), cfg_obj);
    return cfg_obj;
  }

  //---
  [[nodiscard]] conffwk::ConfigObject
  create_queue_obj(const QueueDescriptor* qdesc, std::string uid="") const {
    // conffwk::ConfigObject queue_obj;

    std::string queue_uid(qdesc->get_uid_base() + uid);
    auto queue_obj = create("Queue", queue_uid);
    queue_obj.set_by_val<std::string>("data_type", qdesc->get_data_type());
    queue_obj.set_by_val<std::string>("queue_type", qdesc->get_queue_type());
    queue_obj.set_by_val<uint32_t>("capacity", qdesc->get_capacity());

    return queue_obj;
  }

  //---
  [[nodiscard]] conffwk::ConfigObject
  create_queue_sid_obj(const QueueDescriptor* qdesc, uint32_t src_id) const {
    std::string queue_uid(fmt::format("{}{}", qdesc->get_uid_base(), src_id));
    auto queue_obj = create("QueueWithSourceId", queue_uid);

    queue_obj.set_by_val<std::string>("data_type", qdesc->get_data_type());
    queue_obj.set_by_val<std::string>("queue_type", qdesc->get_queue_type());
    queue_obj.set_by_val<uint32_t>("capacity", qdesc->get_capacity());
    queue_obj.set_by_val<uint32_t>("source_id", src_id);

    return queue_obj;
  }

  //---
  [[nodiscard]] conffwk::ConfigObject create_queue_sid_obj(const QueueDescriptor* qdesc,
                                             const confmodel::DetectorStream* stream) const {
    return create_queue_sid_obj(qdesc, stream->get_source_id());
  }



  //---

  /**
   * \brief Helper function that gets a network connection config
   *
   * \param uid  Unique ID name of the config object
   * \param ndesc Network connection descriptor object
   *
   * \ret OKS configuration object for the network connection
   */
  [[nodiscard]] conffwk::ConfigObject
  create_net_obj(const NetworkConnectionDescriptor* ndesc,
                 std::string uid) const {
    // conffwk::ConfigObject net_obj;

    auto svc_obj = ndesc->get_associated_service()->config_object();
    std::string net_id = ndesc->get_uid_base() + uid;
    auto net_obj = create("NetworkConnection", net_id);

    net_obj.set_by_val<std::string>("data_type", ndesc->get_data_type());
    net_obj.set_by_val<std::string>("connection_type", ndesc->get_connection_type());
    net_obj.set_obj("associated_service", &svc_obj);

    return net_obj;

  }

  [[nodiscard]] conffwk::ConfigObject create_net_obj(const NetworkConnectionDescriptor* ndesc) const {
    return create_net_obj(ndesc, this->m_app_uid);
  }

};

} //namespace dunedaq::appmodel

#endif // APPMODEL_INCLUDE_OBJECTFACTORY_HPP_
