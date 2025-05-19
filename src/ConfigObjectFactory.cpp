
#include "ConfigObjectFactory.hpp"

namespace dunedaq {
namespace appmodel {

ConfigObjectFactory::ConfigObjectFactory(conffwk::Configuration* config,
        const std::string& dbfile,
        const std::string& app_uid)
    : m_config(config),
    m_dbfile(dbfile),
    m_app_uid(app_uid) {

    //FIXME: remove this hacky hack
    oks::OksFile::set_nolock_mode(true);
}



ConfigObjectFactory::ConfigObjectFactory(const conffwk::DalObject* parent) :
    m_config(&parent->configuration()),
    m_dbfile(parent->config_object().contained_in()),
    m_app_uid(parent->UID()) {

    //FIXME: remove this hacky hack
    oks::OksFile::set_nolock_mode(true);
}

ConfigObjectFactory::~ConfigObjectFactory() {
    //FIXME: remove this hacky hack
    oks::OksFile::set_nolock_mode(false);
}

conffwk::ConfigObject 
ConfigObjectFactory::create(const std::string& class_name,
                                     const std::string& id) const {
    conffwk::ConfigObject cfg_obj;
    m_config->create(m_dbfile, class_name, id, cfg_obj);
    return cfg_obj;
}

//---
conffwk::ConfigObject
ConfigObjectFactory::create_queue_obj(const QueueDescriptor* qdesc, std::string uid) const {

    std::string queue_uid(qdesc->get_uid_base() + uid);
    auto queue_obj = create("Queue", queue_uid);
    queue_obj.set_by_val<std::string>("data_type", qdesc->get_data_type());
    queue_obj.set_by_val<std::string>("queue_type", qdesc->get_queue_type());
    queue_obj.set_by_val<uint32_t>("capacity", qdesc->get_capacity());

    return queue_obj;
}

//---
conffwk::ConfigObject
ConfigObjectFactory::create_queue_sid_obj(const QueueDescriptor* qdesc, uint32_t src_id) const {
    std::string queue_uid(fmt::format("{}{}", qdesc->get_uid_base(), src_id));
    auto queue_obj = create("QueueWithSourceId", queue_uid);

    queue_obj.set_by_val<std::string>("data_type", qdesc->get_data_type());
    queue_obj.set_by_val<std::string>("queue_type", qdesc->get_queue_type());
    queue_obj.set_by_val<uint32_t>("capacity", qdesc->get_capacity());
    queue_obj.set_by_val<uint32_t>("source_id", src_id);

    return queue_obj;
}

//---
conffwk::ConfigObject 
ConfigObjectFactory::create_queue_sid_obj(const QueueDescriptor* qdesc,
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
conffwk::ConfigObject
ConfigObjectFactory::create_net_obj(const NetworkConnectionDescriptor* ndesc,
         std::string app_uid) const {

    auto svc_obj = ndesc->get_associated_service()->config_object();
    std::string net_id = ndesc->get_uid_base() + app_uid;
    auto net_obj = create("NetworkConnection", net_id);

    net_obj.set_by_val<std::string>("data_type", ndesc->get_data_type());
    net_obj.set_by_val<std::string>("connection_type", ndesc->get_connection_type());
    net_obj.set_obj("associated_service", &svc_obj);

    return net_obj;
}

conffwk::ConfigObject
ConfigObjectFactory::create_net_obj(const NetworkConnectionDescriptor* ndesc) const {
    return create_net_obj(ndesc, this->m_app_uid);
}

} // namespace appmodel
} // namespace dunedaq
