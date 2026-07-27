/**
 * @file ConfigObjectFactory_test.cxx  Unit Tests for ConfigObjectFactory
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2025.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#define BOOST_TEST_MODULE config_object_factory // NOLINT

#include "boost/test/unit_test.hpp"

#include "../src/ConfigObjectFactory.hpp"
#include "appmodel/DataMoveCallbackConf.hpp"
#include "appmodel/DFOApplication.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "conffwk/Configuration.hpp"
#include "confmodel/QueueWithSourceId.hpp"

#include <cstdio>
#include <list>
#include <string>
#include <thread>


using namespace dunedaq;
using namespace dunedaq::appmodel;

struct fact {
  fact() : confdb("oksconflibs") {
    oksfile = std::tmpnam(nullptr);
    confdb.create(oksfile, includes);
    // ConfigObjectFactory constructor needs a smart daq app so
    // arbitrarily create a DFOApplication
    confdb.create(oksfile, "DFOApplication", app_name, conf_obj);
    dfo = confdb.get<DFOApplication>(conf_obj);
  }

  ~fact() {
    std::remove(oksfile.c_str());
  }

  std::string oksfile;
  conffwk::Configuration confdb;
  const std::list<std::string> includes{
    "schema/confmodel/dunedaq.schema.xml",
    "schema/appmodel/application.schema.xml"};
  const std::string app_name{"DFO-01"};
  conffwk::ConfigObject conf_obj;
  const DFOApplication* dfo;
};


BOOST_FIXTURE_TEST_SUITE(ConfigObjectFactory_test, fact)

BOOST_AUTO_TEST_CASE(simple_create){
  ConfigObjectFactory factory(dfo);

  auto obj = factory.create("Queue", "wibble");
  BOOST_CHECK(!obj.is_null());
  BOOST_CHECK(obj.UID() == "wibble");
}

BOOST_AUTO_TEST_CASE(simple_get_dal){
  ConfigObjectFactory factory(dfo);

  auto obj = factory.create("Queue", "wibble");
  BOOST_CHECK(!obj.is_null());
  BOOST_CHECK(obj.UID() == "wibble");

  auto dal_obj = factory.get_dal<confmodel::Queue>("wibble");
  BOOST_CHECK(dal_obj != nullptr);
  BOOST_CHECK(dal_obj->UID() == "wibble");

  dal_obj = factory.get_dal<confmodel::Queue>(obj);
  BOOST_CHECK(dal_obj != nullptr);
  BOOST_CHECK(dal_obj->UID() == "wibble");
}

BOOST_AUTO_TEST_CASE(bad_descriptor){
  ConfigObjectFactory factory(dfo);

  QueueDescriptor* qdesc{nullptr};
  conffwk::ConfigObject cobj;
  BOOST_CHECK_THROW(cobj = factory.create_queue_obj(qdesc, "bad"), BadConf);
  BOOST_CHECK_THROW(cobj = factory.create_queue_sid_obj(qdesc, 123), BadConf);
  auto stream_obj = factory.create("DetectorStream", "stream");
  stream_obj.set_by_val<int>("source_id", 1234);
  auto stream_dal = confdb.get<confmodel::DetectorStream>(stream_obj);
  BOOST_CHECK_THROW(cobj = factory.create_queue_sid_obj(qdesc, stream_dal), BadConf);

  NetworkConnectionDescriptor* ndesc{nullptr};
  BOOST_CHECK_THROW(cobj = factory.create_net_obj(ndesc, "conn1"), BadConf);
  BOOST_CHECK_THROW(cobj = factory.create_net_obj(ndesc), BadConf);

  DataMoveCallbackDescriptor* cdesc{nullptr};
  BOOST_CHECK_THROW(cobj = factory.create_callback_sid_obj(cdesc, 123), BadConf);
}

BOOST_AUTO_TEST_CASE(qdescriptor){
  ConfigObjectFactory factory(dfo);

  auto qdesc_obj = factory.create("QueueDescriptor", "qdesc");
  qdesc_obj.set_by_val<std::string>("data_type", "test_type");
  qdesc_obj.set_by_val<std::string>("uid_base", "test_uid");
  auto qd_dal = confdb.get<QueueDescriptor>(qdesc_obj);

  auto qobj = factory.create_queue_obj(qd_dal);
  BOOST_CHECK(!qobj.is_null());
  BOOST_CHECK(qobj.UID() == "test_uid");

  auto qobj1 = factory.create_queue_obj(qd_dal, "77");
  BOOST_CHECK(!qobj1.is_null());
  BOOST_CHECK(qobj1.UID() == "test_uid77");
}

BOOST_AUTO_TEST_CASE(qsid_descriptor){
  ConfigObjectFactory factory(dfo);

  auto qdesc_obj = factory.create("QueueDescriptor", "qdesc");
  qdesc_obj.set_by_val<std::string>("data_type", "test_type");
  qdesc_obj.set_by_val<std::string>("uid_base", "test_uid");
  auto qd_dal = confdb.get<QueueDescriptor>(qdesc_obj);

  auto qobj = factory.create_queue_sid_obj(qd_dal, 1234);
  BOOST_CHECK(!qobj.is_null());
  BOOST_CHECK(qobj.UID() == "test_uid1234");
  auto qdal = confdb.get<confmodel::QueueWithSourceId>(qobj);
  BOOST_CHECK (qdal->get_source_id() == 1234);
}

BOOST_AUTO_TEST_CASE(callback_sid_descriptor){
  ConfigObjectFactory factory(dfo);

  auto cdesc_obj = factory.create("DataMoveCallbackDescriptor", "cdesc");
  cdesc_obj.set_by_val<std::string>("data_type", "test_type");
  cdesc_obj.set_by_val<std::string>("uid_base", "test_uid");
  auto cdesc_dal = confdb.get<DataMoveCallbackDescriptor>(cdesc_obj);

  auto cobj = factory.create_callback_sid_obj(cdesc_dal, 1234);
  BOOST_CHECK(!cobj.is_null());
  BOOST_CHECK(cobj.UID() == "test_uid1234");
  auto cdal = confdb.get<DataMoveCallbackConf>(cobj);
  BOOST_CHECK (cdal->get_source_id() == 1234);
}

BOOST_AUTO_TEST_CASE(qsid_stream_descriptor){
  ConfigObjectFactory factory(dfo);

  auto qdesc_obj = factory.create("QueueDescriptor", "qdesc");
  qdesc_obj.set_by_val<std::string>("data_type", "test_type");
  qdesc_obj.set_by_val<std::string>("uid_base", "test_uid");
  auto qd_dal = confdb.get<QueueDescriptor>(qdesc_obj);

  auto stream_obj = factory.create("DetectorStream", "stream");
  stream_obj.set_by_val<int>("source_id", 1234);
  auto stream_dal = confdb.get<confmodel::DetectorStream>(stream_obj);

  auto qobj = factory.create_queue_sid_obj(qd_dal, stream_dal);
  BOOST_CHECK(!qobj.is_null());
  BOOST_CHECK(qobj.UID() == "test_uid1234");
  auto qdal = confdb.get<confmodel::QueueWithSourceId>(qobj);
  BOOST_CHECK (qdal->get_source_id() == 1234);
}

BOOST_AUTO_TEST_CASE(network_connection){
  ConfigObjectFactory factory(dfo);

  auto svc_obj = factory.create("Service", "svc-1");

  auto ndesc_obj = factory.create("NetworkConnectionDescriptor", "ndesc");
  ndesc_obj.set_by_val<std::string>("data_type", "test_type");
  ndesc_obj.set_by_val<std::string>("uid_base", "test-net");
  ndesc_obj.set_obj("associated_service", &svc_obj);
  auto nd_dal = confdb.get<NetworkConnectionDescriptor>(ndesc_obj);

  auto net_obj = factory.create_net_obj(nd_dal, "conn1");
  BOOST_CHECK(!net_obj.is_null());
  BOOST_CHECK(net_obj.UID() == "test-netconn1");

  net_obj = factory.create_net_obj(nd_dal);
  BOOST_CHECK(!net_obj.is_null());
  BOOST_CHECK(net_obj.UID() == "test-net"+app_name);
}

BOOST_AUTO_TEST_CASE(module_update){
  ConfigObjectFactory factory(dfo);

  BOOST_CHECK(dfo->get_modules().size() == 0);
  auto mod = factory.create("DFOModule", "mod-1");
  std::vector<const confmodel::DaqModule*> modules{
    confdb.get<confmodel::DaqModule>(mod)};
  factory.update_modules(modules);
  BOOST_CHECK(dfo->get_modules().size() == 1);
}

BOOST_AUTO_TEST_SUITE_END()
