/**
 * @file disabled_test.cxx Test application that tests advanced features of the disable logic.
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */


#define BOOST_TEST_MODULE disabled_test // NOLINT

#include "boost/test/unit_test.hpp"

#include "conffwk/Configuration.hpp"
#include "confmodel/Session.hpp"
#include "confmodel/Application.hpp"

#include "appmodel/TPStreamWriterApplication.hpp"
#include "appmodel/ReadoutApplication.hpp"

BOOST_AUTO_TEST_SUITE(Disabled_Test)

using namespace dunedaq;

BOOST_AUTO_TEST_CASE(TPStreamWriter) {

  auto db = std::make_shared<conffwk::Configuration>("oksconflibs:test/config/complex_session.data.xml");

  auto session = db->get<confmodel::Session>("local-2x3-config");

  BOOST_REQUIRE(session);
  
  auto apps = session->all_applications();

  const appmodel::TPStreamWriterApplication* tpw = nullptr;
  
  auto counter = 0;
  for (auto a : apps) {
    auto temp = a->cast<appmodel::TPStreamWriterApplication>();
    if (temp) {
      tpw=temp;
      ++counter;
    }
    
  }

  BOOST_REQUIRE(counter==1);

  BOOST_REQUIRE(! tpw->is_disabled(*session) );

  for (auto a : apps) {
    auto temp = a->cast<appmodel::ReadoutApplication>();
    if (temp) {
      const_cast<confmodel::Session*>(session)->disable(temp);
    }
    
  }

  BOOST_CHECK(tpw->is_disabled(*session) ) ;  //this is supposed to fail until changes from Henry are in
}
  
BOOST_AUTO_TEST_SUITE_END()
