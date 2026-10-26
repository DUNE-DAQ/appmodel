/**
 * @file ConfigurationHelper_test.cxx Unit tests for ConfigurationHelper
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "appmodel/ConfigurationHelper.hpp"
#include "conffwk/Configuration.hpp"
#include "confmodel/Session.hpp"
#include "daqdataformats/SourceID.hpp"
#include "daqdataformats/SourceIDGeoIDMap.hpp"
#include "detdataformats/DetID.hpp"
#include "detdataformats/GeoID.hpp"

#define BOOST_TEST_MODULE ConfigurationHelper_test // NOLINT

#include "boost/test/unit_test.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace dunedaq;

BOOST_AUTO_TEST_SUITE(ConfigurationHelper_test)

namespace {

// test/config/configurationhelper_test.data.xml has three ReadoutApplications:
//   ru-a (root segment):   conn-a1 {stream-a1: sid 1, geo 3/1/0/0; stream-a2: sid 2, geo 3/1/0/1}
//                          conn-a2 {stream-a3: sid 2, geo 3/1/1/0}
//   ru-b (root segment):   conn-b1 {stream-b1: sid 10, geo 10/2/0/0}
//                          conn-b2 {stream-b2: sid 11, geo 10/2/0/1}
//   ru-off (off-segment):  conn-off {stream-off: sid 20, geo 3/9/0/0}
// plus a DaqApplication that isn't a ReadoutApplication. The sessions differ only in what they exclude.
const std::string config_db = "oksconflibs:test/config/configurationhelper_test.data.xml";

using expected_t = std::map<uint32_t, std::set<detdataformats::GeoID>>; // NOLINT(build/unsigned)

detdataformats::GeoID
geo(uint16_t det, uint16_t crate, uint16_t slot, uint16_t stream) // NOLINT(build/unsigned)
{
  return detdataformats::GeoID(static_cast<detdataformats::DetID::Subdetector>(det), crate, slot, stream);
}

daqdataformats::SourceIDGeoIDMap
get_map(const std::string& session_name)
{
  conffwk::Configuration confdb(config_db);
  auto session = confdb.get<confmodel::Session>(session_name);
  BOOST_REQUIRE(session != nullptr);
  appmodel::ConfigurationHelper helper(session);
  return helper.get_source_id_geo_id_map();
}

void
check_map(const daqdataformats::SourceIDGeoIDMap& geo_id_map, const expected_t& expected)
{
  BOOST_REQUIRE_EQUAL(geo_id_map.get_entries().size(), expected.size());
  for (auto const& [source_id, geo_ids] : geo_id_map.get_entries()) {
    BOOST_CHECK(source_id.subsystem == daqdataformats::SourceID::Subsystem::kDetectorReadout);
    auto found = expected.find(source_id.id);
    BOOST_REQUIRE_MESSAGE(found != expected.end(), "unexpected SourceID " << source_id);
    BOOST_CHECK_EQUAL(geo_ids.size(), found->second.size());
    BOOST_CHECK((std::set<detdataformats::GeoID>(geo_ids.begin(), geo_ids.end()) == found->second));
  }
}

} // namespace

BOOST_AUTO_TEST_CASE(AllStreamsWhenNothingExcluded)
{
  auto geo_id_map = get_map("all-included");
  check_map(geo_id_map,
            { { 1, { geo(3, 1, 0, 0) } },
              { 2, { geo(3, 1, 0, 1), geo(3, 1, 1, 0) } },
              { 10, { geo(10, 2, 0, 0) } },
              { 11, { geo(10, 2, 0, 1) } },
              { 20, { geo(3, 9, 0, 0) } } });
}

BOOST_AUTO_TEST_CASE(EncodedFormMatchesStoredLayout)
{
  auto encoded = get_map("all-included").to_encoded();
  daqdataformats::SourceID sid(daqdataformats::SourceID::Subsystem::kDetectorReadout, 1);
  BOOST_REQUIRE_EQUAL(encoded.count(sid), 1);
  BOOST_REQUIRE_EQUAL(encoded[sid].size(), 1);
  // stream << 48 | slot << 32 | crate << 16 | detector, as written to raw data files
  BOOST_CHECK_EQUAL(encoded[sid][0], (uint64_t{ 1 } << 16) | uint64_t{ 3 }); // NOLINT(build/unsigned)
}

BOOST_AUTO_TEST_CASE(ExcludedSegmentConnectionAndStreamAreSkipped)
{
  // excludes off-segment, conn-b2 and stream-a2
  auto geo_id_map = get_map("some-excluded");
  check_map(geo_id_map, { { 1, { geo(3, 1, 0, 0) } }, { 2, { geo(3, 1, 1, 0) } }, { 10, { geo(10, 2, 0, 0) } } });
}

BOOST_AUTO_TEST_CASE(ExcludedApplicationIsSkipped)
{
  auto geo_id_map = get_map("app-excluded");
  check_map(geo_id_map,
            { { 1, { geo(3, 1, 0, 0) } }, { 2, { geo(3, 1, 0, 1), geo(3, 1, 1, 0) } }, { 20, { geo(3, 9, 0, 0) } } });
}

// In session duplicate-geo-id, stream-dup (SourceID 30) and stream-dup2 (SourceID 31) have the same
// GeoID values: a configuration error that must be caught before any data is written
BOOST_AUTO_TEST_CASE(GeoIDWithTwoSourceIDsIsRejected)
{
  BOOST_REQUIRE_THROW(get_map("duplicate-geo-id"), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
