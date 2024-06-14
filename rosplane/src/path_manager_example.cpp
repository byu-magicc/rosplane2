//#include "path_manager_example.h"
//#include "ros/ros.h"
#include "path_manager_example.hpp"
#include "Eigen/src/Core/Matrix.h"
#include "rclcpp/rclcpp.hpp"
#include <cmath>
#include <rclcpp/logging.hpp>
#include <string>
#include <chrono>

namespace rosplane
{

path_manager_example::path_manager_example()
    : path_manager_base()
{
  fil_state_ = fillet_state::STRAIGHT;
  dub_state_ = dubin_state::FIRST;

  rclcpp::QoS qos_transient_local_10_(10);
  qos_transient_local_10_.transient_local();
  target_wp_pub_ = this->create_publisher<rosplane_msgs::msg::Waypoint>("target_waypoint", qos_transient_local_10_);

  // Declare the parameters used in this class with the ROS2 system
  declare_parameters();
  params.set_parameters();

  start_time = std::chrono::system_clock::now();

  update_marker_ = true;
}

void path_manager_example::manage(const input_s & input, output_s & output)
{
  // For readability, declare the parameters that will be used in the function here
  double R_min = params.get_double("R_min");
  double default_altitude = params.get_double("default_altitude"); // This is the true altitude not the down position (no need for a negative)
  double default_airspeed = params.get_double("default_airspeed");

  if (num_waypoints_ == 0) 
  {
    auto now = std::chrono::system_clock::now();
    if (float(std::chrono::system_clock::to_time_t(now) - std::chrono::system_clock::to_time_t(start_time)) >= 10.0) 
    { 
      // TODO: Add check to see if the aircraft has been armed. If not just send the warning once before flight then on the throttle after.
      RCLCPP_WARN_STREAM_THROTTLE(this->get_logger(), *this->get_clock(), 5000, "No waypoints received, orbiting origin at " << default_altitude << " meters.");
      output.flag = false; // Indicate that the path is an orbit.
      output.va_d = default_airspeed; // Set to the default_airspeed.
      output.c[0] = 0.0f; // Direcct the center of the orbit to the origin at the default default_altitude.
      output.c[1] = 0.0f;
      output.c[2] = -default_altitude;
      output.rho = R_min; // Make the orbit at the minimum turn radius.
      output.lamda = 1; // Orbit in a clockwise manner.
    }
  }
  else if (num_waypoints_ == 1) 
  {
    // If only a single waypoint is given, orbit it.
    output.flag = false;
    output.va_d = waypoints_[0].va_d;
    output.c[0] = waypoints_[0].w[0];
    output.c[1] = waypoints_[0].w[1];
    output.c[2] = waypoints_[0].w[2];
    output.rho = R_min;
    output.lamda = orbit_direction(input.pn, input.pe, input.chi, output.c[0], output.c[1]); // Calculate the most conveinent orbit direction of that point.
  }
  else {
    if (waypoints_[idx_a_].use_chi) {
      manage_dubins(input, output);
    } else { // If the heading through the point does not matter use the default path following.
      /** Switch the following for flying directly to waypoints, or filleting corners */
      //manage_line(input, output); // TODO add ROS param for just line following or filleting?
      manage_fillet(input, output);
    }
  }
}

void path_manager_example::manage_line(const input_s & input,
                                       output_s & output)
{
  // For readability, declare the parameters that will be used in the function here
  bool orbit_last = params.get_bool("orbit_last");

  Eigen::Vector3f p;
  p << input.pn, input.pe, -input.h;

  int idx_b;
  int idx_c;

  increment_indices(idx_a_, idx_b, idx_c, input, output);

  if (orbit_last && (idx_a_ == num_waypoints_ - 1 || idx_a_ == num_waypoints_ -2))
  {
    return;
  }

  Eigen::Vector3f w_im1(waypoints_[idx_a_].w);
  Eigen::Vector3f w_i(waypoints_[idx_b].w);
  Eigen::Vector3f w_ip1(waypoints_[idx_c].w);
  
  // Fill out data for straight line to the next point.
  output.flag = true;
  output.va_d = waypoints_[idx_a_].va_d;
  output.r[0] = w_im1(0);
  output.r[1] = w_im1(1);
  output.r[2] = w_im1(2);
  Eigen::Vector3f q_im1 = (w_i - w_im1).normalized();
  Eigen::Vector3f q_i = (w_ip1 - w_i).normalized();
  output.q[0] = q_im1(0);
  output.q[1] = q_im1(1);
  output.q[2] = q_im1(2);

  Eigen::Vector3f n_i = (q_im1 + q_i).normalized();

  // Check if the planes were aligned and then handle the normal vector correctly.
  if (n_i.isZero()){ 
    n_i = q_im1;
  }

  // If the aircraft passes through the plane that bisects the angle between the waypoint lines transition.
  if ((p - w_i).dot(n_i) > 0.0f) {
    if (idx_a_ == num_waypoints_ - 1) {
      idx_a_ = 0;
    } else {
      idx_a_++;
    }
    update_marker_ = true;
  }
}

void path_manager_example::manage_fillet(const input_s & input,
                                         output_s & output)
{
  // For readability, declare the parameters that will be used in the function here
  bool orbit_last = params.get_bool("orbit_last");
  double R_min = params.get_double("R_min");

  if (num_waypoints_ < 3) // Do not attempt to fillet between only 2 points.
  {
    manage_line(input, output);
    return;
  }

  Eigen::Vector3f p;
  p << input.pn, input.pe, -input.h;
  
  // idx_a is the waypoint you are coming from.
  int idx_b; // Next waypoint.
  int idx_c; // Waypoint after next.
  
  increment_indices(idx_a_, idx_b, idx_c, input, output);

  if (orbit_last && idx_a_ == num_waypoints_ - 1)
  {
    return;
  }

  Eigen::Vector3f w_im1(waypoints_[idx_a_].w); // Previous waypoint NED im1 means i-1
  Eigen::Vector3f w_i(waypoints_[idx_b].w); // Waypoint the aircraft is headed towards.
  Eigen::Vector3f w_ip1(waypoints_[idx_c].w); // Waypoint after leaving waypoint idx_b.

  output.va_d = waypoints_[idx_a_].va_d; // Desired airspeed of this leg of the waypoints.
  output.r[0] = w_im1(0); // See chapter 11 of the UAV book for more information.
  output.r[1] = w_im1(1); // This is the point that is a point along the commanded path.
  output.r[2] = w_im1(2);
  // The vector pointing into the turn (vector pointing from previous waypoint to the next).
  Eigen::Vector3f q_im1 = (w_i - w_im1); 
  float dist_w_im1 = q_im1.norm();
  q_im1 = q_im1.normalized();

  // The vector pointing out of the turn (vector points from next waypoint to the next next waypoint).
  Eigen::Vector3f q_i = (w_ip1 - w_i); 
  float dist_w_ip1 = q_i.norm();
  q_i = q_i.normalized();

  float varrho = acosf(-q_im1.dot(q_i)); // Angle of the turn.
  
  // Check to see if filleting is possible for given waypoints.
  // Find closest dist to w_i
  float min_dist = std::min(dist_w_ip1, dist_w_im1);

  // Use varrho to find the distance to bisector from closest waypoint.
  float max_r = min_dist * sinf(varrho/2.0);

  // If max_r (maximum radius possible for angle) is smaller than R_min, do line management.
  if (R_min > max_r)
  {
    // While in the too acute region, publish notice every 10 seconds.
    RCLCPP_WARN_STREAM_THROTTLE(this->get_logger(), *this->get_clock(), 5000, "Too acute an angle, using line management. Values, max_r: " << max_r << " R_min: " << R_min);
    manage_line(input, output);
    return;
  }

  Eigen::Vector3f z;
  switch (fil_state_) {
    case fillet_state::STRAIGHT:
    {
      output.flag = true; // Indicate flying a straight path.
      output.q[0] = q_im1(0); // Fly along vector into the turn the origin of the vector is r (set as previous waypoint above).
      output.q[1] = q_im1(1);
      output.q[2] = q_im1(2);
      output.c[0] = 1; // Fill rest of the data though it is not used.
      output.c[1] = 1;
      output.c[2] = 1;
      output.rho = 1;
      output.lamda = 1;
      z = w_i - q_im1 * (R_min / tanf(varrho / 2.0)); // Point in plane where after passing through the aircraft should begin the turn.


      if ((p - z).dot(q_im1) > 0)
      {
        if (q_i == q_im1) // Check to see if the waypoint is directly between the next two.
        {
          if (idx_a_ == num_waypoints_ - 1) {
            idx_a_ = 0;
          }
          else {
            idx_a_++;
          }
          update_marker_ = true;
          break;
        }
        fil_state_ = fillet_state::TRANSITION; // Check to see if passed through the plane.
      }
      break;
    }
    case fillet_state::TRANSITION:
    {
      output.flag = false; // Indicate that aircraft is following an orbit.
      output.q[0] = q_i(0); // Load the message with the vector that will be follwed after the orbit.
      output.q[1] = q_i(1);
      output.q[2] = q_i(2);
      Eigen::Vector3f c = w_i - (q_im1 - q_i).normalized() * (R_min / sinf(varrho / 2.0)); // Calculate the center of the orbit.
      output.c[0] = c(0); // Load message with the center of the orbit.
      output.c[1] = c(1);
      output.c[2] = c(2);
      output.rho = R_min; // Command the orbit radius to be the minimum acheivable.
      output.lamda = ((q_im1(0) * q_i(1) - q_im1(1) * q_i(0)) > 0 ? 1 : -1); // Find the direction to orbit the point.
      z = w_i + q_i * (R_min / tanf(varrho / 2.0)); // Find the point in the plane that once you pass through you should increment the indexes and follow a straight line.

      if (orbit_last && idx_a_ == num_waypoints_ - 2)
      {
        idx_a_++;
        fil_state_ = fillet_state::STRAIGHT;
        break;
      }

      if ((p - z).dot(q_i) < 0) { // Check to see if passed through plane.
        fil_state_ = fillet_state::ORBIT;
      }
      break;
    }
    case fillet_state::ORBIT:
    {
      output.flag = false; // Indicate that aircraft is following an orbit.
      output.q[0] = q_i(0); // Load the message with the vector that will be follwed after the orbit.
      output.q[1] = q_i(1);
      output.q[2] = q_i(2);
      Eigen::Vector3f c = w_i - (q_im1 - q_i).normalized() * (R_min / sinf(varrho / 2.0)); // Calculate the center of the orbit.
      output.c[0] = c(0); // Load message with the center of the orbit.
      output.c[1] = c(1);
      output.c[2] = c(2);
      output.rho = R_min; // Command the orbit radius to be the minimum acheivable.
      output.lamda = ((q_im1(0) * q_i(1) - q_im1(1) * q_i(0)) > 0 ? 1 : -1); // Find the direction to orbit the point. TODO change this to the orbit_direction.
      z = w_i + q_i * (R_min / tanf(varrho / 2.0)); // Find the point in the plane that once you pass through you should increment the indexes and follow a straight line.
      if ((p - z).dot(q_i) > 0) { // Check to see if passed through plane.
        if (idx_a_ == num_waypoints_ - 1) {
          idx_a_ = 0;
        }
        else {
          idx_a_++;
        }
        update_marker_ = true;
        fil_state_ = fillet_state::STRAIGHT;
      }
      break;
    }
  }
}

void path_manager_example::manage_dubins(const input_s & input,
                                         output_s & output)
{
  // For readability, declare the parameters that will be used in the function here
  double R_min = params.get_double("R_min");

  Eigen::Vector3f p;
  p << input.pn, input.pe, -input.h;

  output.va_d = waypoints_[idx_a_].va_d;
  output.r[0] = 0;
  output.r[1] = 0;
  output.r[2] = 0;
  output.q[0] = 0;
  output.q[1] = 0;
  output.q[2] = 0;
  output.c[0] = 0;
  output.c[1] = 0;
  output.c[2] = 0;

  switch (dub_state_) {
    case dubin_state::FIRST:
      dubinsParameters(waypoints_[0], waypoints_[1], R_min);
      output.flag = false;
      output.c[0] = dubinspath_.cs(0);
      output.c[1] = dubinspath_.cs(1);
      output.c[2] = dubinspath_.cs(2);
      output.rho = dubinspath_.R;
      output.lamda = dubinspath_.lams;
      if ((p - dubinspath_.w1).dot(dubinspath_.q1) >= 0) // start in H1
      {
        dub_state_ = dubin_state::BEFORE_H1_WRONG_SIDE;
      } else {
        dub_state_ = dubin_state::BEFORE_H1;
      }
      break;
    case dubin_state::BEFORE_H1:
      output.flag = false;
      output.c[0] = dubinspath_.cs(0);
      output.c[1] = dubinspath_.cs(1);
      output.c[2] = dubinspath_.cs(2);
      output.rho = dubinspath_.R;
      output.lamda = dubinspath_.lams;
      if ((p - dubinspath_.w1).dot(dubinspath_.q1) >= 0) // entering H1
      {
        dub_state_ = dubin_state::STRAIGHT;
      }
      break;
    case dubin_state::BEFORE_H1_WRONG_SIDE:
      output.flag = false;
      output.c[0] = dubinspath_.cs(0);
      output.c[1] = dubinspath_.cs(1);
      output.c[2] = dubinspath_.cs(2);
      output.rho = dubinspath_.R;
      output.lamda = dubinspath_.lams;
      if ((p - dubinspath_.w1).dot(dubinspath_.q1) < 0) // exit H1
      {
        dub_state_ = dubin_state::BEFORE_H1;
      }
      break;
    case dubin_state::STRAIGHT:
      output.flag = true;
      output.r[0] = dubinspath_.w1(0);
      output.r[1] = dubinspath_.w1(1);
      output.r[2] = dubinspath_.w1(2);
      // output.r[0] = dubinspath_.z1(0);
      // output.r[1] = dubinspath_.z1(1);
      // output.r[2] = dubinspath_.z1(2);
      output.q[0] = dubinspath_.q1(0);
      output.q[1] = dubinspath_.q1(1);
      output.q[2] = dubinspath_.q1(2);
      output.rho = 1;
      output.lamda = 1;
      if ((p - dubinspath_.w2).dot(dubinspath_.q1) >= 0) // entering H2
      {
        if ((p - dubinspath_.w3).dot(dubinspath_.q3) >= 0) // start in H3
        {
          dub_state_ = dubin_state::BEFORE_H3_WRONG_SIDE;
        } else {
          dub_state_ = dubin_state::BEFORE_H3;
        }
      }
      break;
    case dubin_state::BEFORE_H3:
      output.flag = false;
      output.c[0] = dubinspath_.ce(0);
      output.c[1] = dubinspath_.ce(1);
      output.c[2] = dubinspath_.ce(2);
      output.rho = dubinspath_.R;
      output.lamda = dubinspath_.lame;
      if ((p - dubinspath_.w3).dot(dubinspath_.q3) >= 0) // entering H3
      {
        // increase the waypoint pointer
        int idx_b;
        if (idx_a_ == num_waypoints_ - 1) {
          idx_a_ = 0;
          idx_b = 1;
        } else if (idx_a_ == num_waypoints_ - 2) {
          idx_a_++;
          idx_b = 0;
        } else {
          idx_a_++;
          idx_b = idx_a_ + 1;
        }
        update_marker_ = true;

        // plan new Dubin's path to next waypoint configuration
        dubinsParameters(waypoints_[idx_a_], waypoints_[idx_b], R_min);

        //start new path
        if ((p - dubinspath_.w1).dot(dubinspath_.q1) >= 0) // start in H1
        {
          dub_state_ = dubin_state::BEFORE_H1_WRONG_SIDE;
        } else {
          dub_state_ = dubin_state::BEFORE_H1;
        }
      }
      break;
    case dubin_state::BEFORE_H3_WRONG_SIDE:
      output.flag = false;
      output.c[0] = dubinspath_.ce(0);
      output.c[1] = dubinspath_.ce(1);
      output.c[2] = dubinspath_.ce(2);
      output.rho = dubinspath_.R;
      output.lamda = dubinspath_.lame;
      if ((p - dubinspath_.w3).dot(dubinspath_.q3) < 0) // exit H3
      {
        dub_state_ = dubin_state::BEFORE_H1;
      }
      break;
  }
}

Eigen::Matrix3f path_manager_example::rotz(float theta)
{
  Eigen::Matrix3f R;
  R << cosf(theta), -sinf(theta), 0, sinf(theta), cosf(theta), 0, 0, 0, 1;

  return R;
}

float path_manager_example::mo(float in)
{
  float val;
  if (in > 0) val = fmod(in, 2.0 * M_PI_F);
  else {
    float n = floorf(in / 2.0 / M_PI_F);
    val = in - n * 2.0 * M_PI_F;
  }
  return val;
}

void path_manager_example::dubinsParameters(const waypoint_s start_node, const waypoint_s end_node,
                                            float R)
{
  float ell = sqrtf((start_node.w[0] - end_node.w[0]) * (start_node.w[0] - end_node.w[0])
                    + (start_node.w[1] - end_node.w[1]) * (start_node.w[1] - end_node.w[1]));
  if (ell < 2.0 * R) {
    //ROS_ERROR("The distance between nodes must be larger than 2R.");
    RCLCPP_ERROR(this->get_logger(), "The distance between nodes must be larger than 2R.");

  } else {
    dubinspath_.ps(0) = start_node.w[0];
    dubinspath_.ps(1) = start_node.w[1];
    dubinspath_.ps(2) = start_node.w[2];
    dubinspath_.chis = start_node.chi_d;
    dubinspath_.pe(0) = end_node.w[0];
    dubinspath_.pe(1) = end_node.w[1];
    dubinspath_.pe(2) = end_node.w[2];
    dubinspath_.chie = end_node.chi_d;

    Eigen::Vector3f crs = dubinspath_.ps;
    crs(0) +=
      R * (cosf(M_PI_2_F) * cosf(dubinspath_.chis) - sinf(M_PI_2_F) * sinf(dubinspath_.chis));
    crs(1) +=
      R * (sinf(M_PI_2_F) * cosf(dubinspath_.chis) + cosf(M_PI_2_F) * sinf(dubinspath_.chis));
    Eigen::Vector3f cls = dubinspath_.ps;
    cls(0) +=
      R * (cosf(-M_PI_2_F) * cosf(dubinspath_.chis) - sinf(-M_PI_2_F) * sinf(dubinspath_.chis));
    cls(1) +=
      R * (sinf(-M_PI_2_F) * cosf(dubinspath_.chis) + cosf(-M_PI_2_F) * sinf(dubinspath_.chis));
    Eigen::Vector3f cre = dubinspath_.pe;
    cre(0) +=
      R * (cosf(M_PI_2_F) * cosf(dubinspath_.chie) - sinf(M_PI_2_F) * sinf(dubinspath_.chie));
    cre(1) +=
      R * (sinf(M_PI_2_F) * cosf(dubinspath_.chie) + cosf(M_PI_2_F) * sinf(dubinspath_.chie));
    Eigen::Vector3f cle = dubinspath_.pe;
    cle(0) +=
      R * (cosf(-M_PI_2_F) * cosf(dubinspath_.chie) - sinf(-M_PI_2_F) * sinf(dubinspath_.chie));
    cle(1) +=
      R * (sinf(-M_PI_2_F) * cosf(dubinspath_.chie) + cosf(-M_PI_2_F) * sinf(dubinspath_.chie));

    float theta, theta2;
    // compute L1
    theta = atan2f(cre(1) - crs(1), cre(0) - crs(0));
    float L1 = (crs - cre).norm()
      + R * mo(2.0 * M_PI_F + mo(theta - M_PI_2_F) - mo(dubinspath_.chis - M_PI_2_F))
      + R * mo(2.0 * M_PI_F + mo(dubinspath_.chie - M_PI_2_F) - mo(theta - M_PI_2_F));

    // compute L2
    ell = (cle - crs).norm();
    theta = atan2f(cle(1) - crs(1), cle(0) - crs(0));
    float L2;
    if (2.0 * R > ell) L2 = 9999.0f;
    else {
      theta2 = theta - M_PI_2_F + asinf(2.0 * R / ell);
      L2 = sqrtf(ell * ell - 4.0 * R * R)
        + R * mo(2.0 * M_PI_F + mo(theta2) - mo(dubinspath_.chis - M_PI_2_F))
        + R * mo(2.0 * M_PI_F + mo(theta2 + M_PI_F) - mo(dubinspath_.chie + M_PI_2_F));
    }

    // compute L3
    ell = (cre - cls).norm();
    theta = atan2f(cre(1) - cls(1), cre(0) - cls(0));
    float L3;
    if (2.0 * R > ell) L3 = 9999.0f;
    else {
      theta2 = acosf(2.0 * R / ell);
      L3 = sqrtf(ell * ell - 4 * R * R)
        + R * mo(2.0 * M_PI_F + mo(dubinspath_.chis + M_PI_2_F) - mo(theta + theta2))
        + R * mo(2.0 * M_PI_F + mo(dubinspath_.chie - M_PI_2_F) - mo(theta + theta2 - M_PI_F));
    }

    // compute L4
    theta = atan2f(cle(1) - cls(1), cle(0) - cls(0));
    float L4 = (cls - cle).norm()
      + R * mo(2.0 * M_PI_F + mo(dubinspath_.chis + M_PI_2_F) - mo(theta + M_PI_2_F))
      + R * mo(2.0 * M_PI_F + mo(theta + M_PI_2_F) - mo(dubinspath_.chie + M_PI_2_F));

    // L is the minimum distance
    int idx = 1;
    dubinspath_.L = L1;
    if (L2 < dubinspath_.L) {
      dubinspath_.L = L2;
      idx = 2;
    }
    if (L3 < dubinspath_.L) {
      dubinspath_.L = L3;
      idx = 3;
    }
    if (L4 < dubinspath_.L) { dubinspath_.L = L4;
      idx = 4;
    }

    Eigen::Vector3f e1;
    //        e1.zero();
    e1(0) = 1;
    e1(1) = 0;
    e1(2) = 0;
    switch (idx) {
      case 1:
        dubinspath_.cs = crs;
        dubinspath_.lams = 1;
        dubinspath_.ce = cre;
        dubinspath_.lame = 1;
        dubinspath_.q1 = (cre - crs).normalized();
        dubinspath_.w1 = dubinspath_.cs + (rotz(-M_PI_2_F) * dubinspath_.q1) * R;
        dubinspath_.w2 = dubinspath_.ce + (rotz(-M_PI_2_F) * dubinspath_.q1) * R;
        break;
      case 2:
        dubinspath_.cs = crs;
        dubinspath_.lams = 1;
        dubinspath_.ce = cle;
        dubinspath_.lame = -1;
        ell = (cle - crs).norm();
        theta = atan2f(cle(1) - crs(1), cle(0) - crs(0));
        theta2 = theta - M_PI_2_F + asinf(2.0 * R / ell);
        dubinspath_.q1 = rotz(theta2 + M_PI_2_F) * e1;
        dubinspath_.w1 = dubinspath_.cs + (rotz(theta2) * e1) * R;
        dubinspath_.w2 = dubinspath_.ce + (rotz(theta2 + M_PI_F) * e1) * R;
        break;
      case 3:
        dubinspath_.cs = cls;
        dubinspath_.lams = -1;
        dubinspath_.ce = cre;
        dubinspath_.lame = 1;
        ell = (cre - cls).norm();
        theta = atan2f(cre(1) - cls(1), cre(0) - cls(0));
        theta2 = acosf(2.0 * R / ell);
        dubinspath_.q1 = rotz(theta + theta2 - M_PI_2_F) * e1;
        dubinspath_.w1 = dubinspath_.cs + (rotz(theta + theta2) * e1) * R;
        dubinspath_.w2 = dubinspath_.ce + (rotz(theta + theta2 - M_PI_F) * e1) * R;
        break;
      case 4:
        dubinspath_.cs = cls;
        dubinspath_.lams = -1;
        dubinspath_.ce = cle;
        dubinspath_.lame = -1;
        dubinspath_.q1 = (cle - cls).normalized();
        dubinspath_.w1 = dubinspath_.cs + (rotz(M_PI_2_F) * dubinspath_.q1) * R;
        dubinspath_.w2 = dubinspath_.ce + (rotz(M_PI_2_F) * dubinspath_.q1) * R;
        break;
    }
    dubinspath_.w3 = dubinspath_.pe;
    dubinspath_.q3 = rotz(dubinspath_.chie) * e1;
    dubinspath_.R = R;
  }
}

void path_manager_example::declare_parameters()
{
  params.declare_param("R_min", 25.0);
  params.declare_param("orbit_last", false);
  params.declare_param("default_altitude", 50.0);
  params.declare_param("default_airspeed", 15.0);
}

int path_manager_example::orbit_direction(float pn, float pe, float chi, float c_n, float c_e)
{
  if (orbit_dir != 0)
  {
    return orbit_dir;
  }

  Eigen::Vector3f pos;
  pos << pn, pe, 0.0;

  Eigen::Vector3f center;
  center << c_n, c_e, 0.0;

  Eigen::Vector3f d = pos - center;

  Eigen::Vector3f course;
  course << sinf(chi), cosf(chi), 0.0;

  if (d.cross(course)(2) >= 0 ) 
  {
    orbit_dir = 1;
    return 1;
  }
  
  orbit_dir = -1;
  return -1;
}

void path_manager_example::increment_indices(int & idx_a, int & idx_b, int & idx_c, const struct input_s & input, struct output_s & output)
{

  bool orbit_last = params.get_bool("orbit_last");
  double R_min = params.get_double("R_min");

  if (temp_waypoint_ && idx_a_ == 1)
  {
    waypoints_.erase(waypoints_.begin());
    num_waypoints_--;
    idx_a_ = 0;
    idx_b = 1;
    idx_c = 2;
    temp_waypoint_ = false;
    update_marker_ = true;
    return;
  }

  if (idx_a == num_waypoints_ - 1) { // The logic for if it is the last waypoint.
     
    // If it is the last waypoint, and we orbit the last waypoint, construct the command.
    if (orbit_last) {
      output.flag = false;
      output.va_d = waypoints_[idx_a_].va_d;
      output.c[0] = waypoints_[idx_a_].w[0];
      output.c[1] = waypoints_[idx_a_].w[1];
      output.c[2] = waypoints_[idx_a_].w[2];
      output.r[0] = 0.0;
      output.r[1] = 0.0;
      output.r[2] = 0.0;
      output.q[0] = 0.0;
      output.q[1] = 0.0;
      output.q[2] = 0.0;
      output.rho = R_min;
      output.lamda = orbit_direction(input.pn, input.pe, input.chi, output.c[0], output.c[1]); // Calculate the most conveinent orbit direction of that point.

      if (update_marker_) {
        publish_target_wp(idx_a_);
        update_marker_ = false;
      }

      idx_b = 0; // reset the path to loop the waypoints if orbit_last is set false
      idx_c = 1;
      return;
    }

    idx_b = 0; // reset the path and loop the waypoints again.
    idx_c = 1;
  } else if (idx_a == num_waypoints_ - 2) { // If the second to last waypoint, appropriately handle the wrapping of waypoints.
    idx_b = num_waypoints_ - 1;
    idx_c = 0;
  } else { // Increment the indices of the waypoints.
    idx_b = idx_a + 1;
    idx_c = idx_b + 1;
  }

  if (update_marker_) {
    publish_target_wp(idx_b);
    update_marker_ = false;
  }
}

void path_manager_example::publish_target_wp(int idx) {
  // Publish the target waypoint for visualization
  rosplane_msgs::msg::Waypoint target_wp;
  target_wp.w[0] = waypoints_[idx].w[0];
  target_wp.w[1] = waypoints_[idx].w[1];
  target_wp.w[2] = waypoints_[idx].w[2];
  target_wp.va_d = waypoints_[idx].va_d;
  target_wp.lla = false;
  target_wp_pub_->publish(target_wp);
}

} // namespace rosplane
