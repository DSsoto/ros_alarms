#include <ros_alarms/listener.hpp>
#include <ros_alarms/broadcaster.hpp>
#include <iostream>
#include <functional>
#include <gtest/gtest.h>

using namespace std;
using ros_alarms::Alarm;
using ros_alarms::AlarmProxy;
using ros_alarms::AlarmListener;
using ros_alarms::AlarmBroadcaster;

TEST(ServerCheck, setServiceCheck)
{
  // Check for existence of alarm server
  ros::Time::init();  // needed to use ros::Time::now() w/o creating a  NodeHandle
  ros::Duration time_out {2, 0};
  ASSERT_TRUE(ros::service::waitForService("/alarm/set", time_out))
    << "/alarm/set service not available";
  
  return;
}

TEST(ServerCheck, getServiceCheck)
{
  // Check for existence of alarm server
  ros::Time::init();  // needed to use ros::Time::now() w/o creating a  NodeHandle
  ros::Duration time_out {2, 0};
  ASSERT_TRUE(ros::service::waitForService("/alarm/get", time_out))
    << "/alarm/get service not available";
  return;
}

class AlarmTest : public ::testing::Test
{
public:
  ros::NodeHandle nh;
  string alarm_name = "test_alarm";
  string node_name = "test_alarm_client_node";
  AlarmProxy pxy {"test_alarm", false, "test_alarm_client_node", "", "json", 5};
  AlarmTest()
  {
  }
};

TEST_F(AlarmTest, alarmProxyTest)
{
  // Test ctor
  auto ctor_err {"AlarmProxy different field than expected based on value passed to ctor."};
  EXPECT_STREQ("test_alarm", pxy.alarm_name.c_str()) << ctor_err;
  EXPECT_STREQ("test_alarm_client_node", pxy.node_name.c_str()) << ctor_err;
  EXPECT_STREQ("", pxy.problem_description.c_str()) << ctor_err;
  EXPECT_STREQ("json", pxy.json_parameters.c_str()) << ctor_err;
  EXPECT_EQ(5, pxy.severity) << ctor_err;

  // Test conversion ctor
  EXPECT_EQ(pxy, AlarmProxy(pxy.as_msg()))
    << "Proxy was turned into a ros msg and back into a proxy but is different from original";

  // Test operator== and copy cotr
  AlarmProxy pxy2 = pxy;
  EXPECT_EQ(pxy, pxy2)
    << "Operator == returned false when called w/ an AlarmProxy copy constructed from itself";
  AlarmProxy pxy3 = pxy;
  EXPECT_EQ(pxy2, pxy3)
    << "Operator == returned false when called w/ 2 copies of the same AlarmProxy.";
}

TEST_F(AlarmTest, broadcasterTest)
{
  // Construct Kill broadcaster using the 2 different methods
  AlarmBroadcaster a_caster1{nh};            // Manipulates encapsulated AlarmProxy that
  a_caster1.alarm() = pxy;                   //   is a copy of the one in the text fixture
  AlarmBroadcaster a_caster2{nh, &pxy};      // Broadcaster is bound to external AlarmProxy
  AlarmListener<> listener(nh, alarm_name);  // Callback-less listener for testing broadcasters

  a_caster1.clear();
  EXPECT_FALSE(listener.query_raised()) << "'test_alarm' should start out cleared";
  EXPECT_EQ(!listener.is_raised(), listener.is_cleared())
    << "is_raised() returned the same thing as is_cleared()";

  // Test raising and clearing
  auto raise_msg {"Broadcaster wasn't able to raise test_alarm"};
  auto clear_msg {"Broadcaster wasn't able to clear test_alarm"};
  a_caster1.raise();
  EXPECT_TRUE(listener.query_raised()) << raise_msg;
  a_caster1.clear();
  EXPECT_FALSE(listener.query_raised()) << clear_msg;
  a_caster2.raise();
  EXPECT_TRUE(listener.query_raised()) << raise_msg;
  a_caster2.clear();
  EXPECT_FALSE(listener.query_raised()) << clear_msg;

  // Test changing alarm via outside pxy or internal ref to it and updating server
  pxy.severity = 2;                                    // Change external AlarmProxy
  a_caster2.alarm().raised = true;                     // Change internal handle
  EXPECT_EQ(pxy, a_caster2.alarm());                   // And yet, pxy == a_caster2.alarm()
  a_caster2.publish();                                 // Publish changed status to server
  EXPECT_EQ(a_caster2.alarm(), listener.get_alarm())   // Listener should confirm server 
                                                       //   received updates
    << a_caster2.alarm().str(true) + " =/= " + listener.get_alarm().str(true);
  EXPECT_EQ(2, listener.get_alarm().severity)
    << "Unable to use external AlarmProxy to publish new alarm status";

  // Test changing alarm via reference to internal pxy and updating server
  a_caster1.updateSeverity(3);
  EXPECT_EQ(3, listener.get_alarm().severity);
  a_caster1.alarm().problem_description = "There's no problem here";
  a_caster1.publish();
  EXPECT_STREQ("There's no problem here", listener.get_alarm().problem_description.c_str());
  a_caster1.clear();  // make sure alarm starts cleared for following tests
  EXPECT_TRUE(listener.query_cleared());

  return;
}

TEST_F(AlarmTest, listenerTest)
{
  // Create broadcaster & listener
  // You can skip the template args for any
  AlarmListener<> listener{nh, alarm_name};  // You can omit template args for any cb that can
  EXPECT_TRUE(listener.ok());                // be put into a std::function<void(AlarmProxy)>
  AlarmBroadcaster ab(nh);
  ab.alarm() = pxy;
  ab.clear();  // alarm starts off cleared

  // Last update time happened wehen we called ab.clear()
  auto first_query = listener.last_update_time();

  ab.updateSeverity(5); // This is an update to the alarm

  // The listener isn't querying the server before returning the status on these next
  // two lines, It is returning the current status of the internal AlarmProxy object,
  // which is not updated by these calls.
  listener.is_raised();
  listener.is_cleared();

  // Last update time should not have changed because of calls to is_raised() or is_cleared()
  // Unless you are unlucky enough to have an update cb in that short time
  EXPECT_EQ(first_query, listener.last_update_time());
  EXPECT_EQ(listener.get_cached_alarm().raised, listener.is_raised());

  // The following query_* functions query the server before reporting the status, so the
  // last update time should have changed
  listener.query_raised();
  listener.query_cleared();
  EXPECT_NE(first_query, listener.last_update_time());

#define PRINT(x) cerr << #x ":" << x  << endl; // For debugging in case of test failures
#define COUNTS() {                  \
  cerr << endl;                     \
  PRINT(update_count)               \
  PRINT(lo_priority_raise_count)    \
  PRINT(hi_priority_raise_count)    \
  PRINT(exact_priority_raise_count) \
  PRINT(raise_count)                \
  PRINT(clear_count)                \
  }
  // Setup callback functions
  int update_count= 0;                  // any updates
  int lo_priority_raise_count = 0;      // raises w/ priority in [0,2]
  int hi_priority_raise_count = 0;      // raises w/ priority in [4,5]
  int exact_priority_raise_count = 0;   // raises w/ priority of exactly 3
  int raise_count = 0;                  // raises of any priority
  int clear_count = 0;                  // any clears
  function<void(AlarmProxy)>  // All cb funcs for this listener need to have this signature
    update_cb = [&update_count](AlarmProxy pxy) -> void { ++update_count; };
  function<void(AlarmProxy)> lo_raise_cb  =
    [&lo_priority_raise_count ](AlarmProxy pxy) -> void { ++lo_priority_raise_count; };
  function<void(AlarmProxy)> hi_raise_cb  =
    [&hi_priority_raise_count ](AlarmProxy pxy) -> void { ++hi_priority_raise_count; };
  function<void(AlarmProxy)> exact_raise_cb  =
    [&exact_priority_raise_count ](AlarmProxy pxy) -> void { ++exact_priority_raise_count; };
  function<void(AlarmProxy)> raise_cb =
    [&raise_count](AlarmProxy pxy) -> void { ++raise_count; };
  function<void(AlarmProxy)> clear_cb =
    [&clear_count](AlarmProxy pxy) -> void { ++clear_count; };

  // Make sure initial conditians are good for testing callbacks
  ab.alarm().raised = false;
  ab.alarm().severity = 0;
  ab.publish();
  ASSERT_FALSE(listener.query_raised());
  ASSERT_EQ(0, listener.get_cached_alarm().severity);
  ASSERT_EQ(0, update_count);
  ASSERT_TRUE(update_count == lo_priority_raise_count == hi_priority_raise_count
    == exact_priority_raise_count == raise_count == clear_count);

  // Add callbacks to listener
  ros::spinOnce();                           // Clear callback queue just in case
  listener.clear_callbacks();
  listener.add_cb(update_cb);                // Called for any update of the alarm
  listener.add_raise_cb(lo_raise_cb, 0, 2);  // Last 2 args specify severity range
  listener.add_raise_cb(hi_raise_cb, 4, 5);  // Last 2 args specify severity range
  listener.add_raise_cb(exact_raise_cb, 3);  // Use this overload for a single severity
  listener.add_raise_cb(raise_cb);           // Use this overload for any severity raise
  listener.add_clear_cb(clear_cb);           // Called for any clear of the alarm

  // Go crazy raising and clearing!
  ros::Duration latency(0.001);  // Approximate upper bound on publisher latency
  ab.updateSeverity(0);
  latency.sleep();  // Make sure listener has time to receive published updates
  ros::spinOnce();  // Process ros callback queue
  EXPECT_EQ(1, update_count);
  EXPECT_EQ(1, lo_priority_raise_count);
  EXPECT_EQ(1, raise_count);
  ab.clear();
  latency.sleep();
  ros::spinOnce();
  EXPECT_EQ(2, update_count);
  EXPECT_EQ(1, clear_count);
  ab.updateSeverity(1);
  latency.sleep();
  ros::spinOnce();
  EXPECT_EQ(3, update_count);
  EXPECT_EQ(2, lo_priority_raise_count);
  EXPECT_EQ(2, raise_count);
  ab.clear();
  latency.sleep();
  ros::spinOnce();
  EXPECT_EQ(4, update_count);
  EXPECT_EQ(2, clear_count);
  ab.updateSeverity(2);
  latency.sleep();
  ros::spinOnce();
  EXPECT_EQ(5, update_count);
  EXPECT_EQ(3, lo_priority_raise_count);
  EXPECT_EQ(3, raise_count);
  ab.clear();
  latency.sleep();
  ros::spinOnce();
  EXPECT_EQ(6, update_count);
  EXPECT_EQ(3, clear_count);
  ab.updateSeverity(3);
  latency.sleep();
  ros::spinOnce();
  EXPECT_EQ(7, update_count);
  EXPECT_EQ(1, exact_priority_raise_count);
  EXPECT_EQ(4, raise_count);
  ab.clear();
  latency.sleep();
  ros::spinOnce();
  EXPECT_EQ(8, update_count);
  EXPECT_EQ(4, clear_count);
  ab.updateSeverity(4);
  latency.sleep();
  ros::spinOnce();
  EXPECT_EQ(9, update_count);
  EXPECT_EQ(1, hi_priority_raise_count);
  EXPECT_EQ(5, raise_count);
  ab.clear();
  latency.sleep();
  ros::spinOnce();
  EXPECT_EQ(10, update_count);
  EXPECT_EQ(5, clear_count);
  ab.updateSeverity(5);
  latency.sleep();
  ros::spinOnce();
  EXPECT_EQ(11, update_count);
  EXPECT_EQ(2, hi_priority_raise_count);
  EXPECT_EQ(6, raise_count);
  EXPECT_EQ(5, clear_count);
  ab.clear();
  latency.sleep();
  ros::spinOnce();
  EXPECT_EQ(12, update_count);
  EXPECT_EQ(3, lo_priority_raise_count);
  EXPECT_EQ(2, hi_priority_raise_count);
  EXPECT_EQ(1, exact_priority_raise_count);
  EXPECT_EQ(6, raise_count);
  EXPECT_EQ(6, clear_count);
  return;
}

//TEST_F(AlarmTest, heartbeatMonitorTest)
//{
//
//  return;
//}
