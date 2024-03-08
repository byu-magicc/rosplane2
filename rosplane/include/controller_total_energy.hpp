#ifndef BUILD_CONTROLLER_TOTAL_ENERGY_H
#define BUILD_CONTROLLER_TOTAL_ENERGY_H

#include "controller_successive_loop.hpp"

namespace rosplane
{

class controller_total_energy : public controller_successive_loop
{
public:
  /**
   * Constructor to initialize node.
   */
  controller_total_energy();

protected:
  /**
   * This function overrides the longitudinal control loops for the take-off zone.
   * @param params The parameters that define the algorithm such as control gains.
   * @param input The command inputs to the controller such as course and airspeed.
   * @param output The control efforts calculated and selected intermediate values.
   */
  virtual void take_off_longitudinal_control(const struct params_s & params,
                                             const struct input_s & input,
                                             struct output_s & output);

  /**
   * This function overrides the longitudinal control loops for the climb zone.
   * @param params The parameters that define the algorithm such as control gains.
   * @param input The command inputs to the controller such as course and airspeed.
   * @param output The control efforts calculated and selected intermediate values.
   */
  virtual void climb_longitudinal_control(const struct params_s & params,
                                          const struct input_s & input, struct output_s & output);

  /**
   * This function overrides the longitudinal control loops for the altitude hold zone.
   * @param params The parameters that define the algorithm such as control gains.
   * @param input The command inputs to the controller such as course and airspeed.
   * @param output The control efforts calculated and selected intermediate values.
   */
  virtual void alt_hold_longitudinal_control(const struct params_s & params,
                                             const struct input_s & input,
                                             struct output_s & output);

  /**
   * This function overrides when the aircraft exits the take-off zone. Any changes to the controller that need to happen
   * only once as the aircraft exits take-off mode should be placed here. This sets differentiators and integrators to 0.
   */
  virtual void take_off_exit();

  /**
   * This function overrides when the aircraft exits the climb zone. Any changes to the controller that need to happen
   * only once as the aircraft exits climb mode should be placed here. This sets differentiators and integrators to 0.
   */
  virtual void climb_exit();

  /**
   * This function overrides when the aircraft exits the altitude hold zone (usually a crash). Any changes to the controller that 
   * need to happen only once as the aircraft exits altitude mode should be placed here. This sets differentiators and
   * integrators to 0.
   */
  virtual void altitude_hold_exit();

  /**
   * This uses the error in total energy to find the necessary throttle to acheive that energy.
   * @param va_c This is the commanded airspeed.
   * @param va This is the actual airspeed.
   * @param h_c This is the commanded altitude.
   * @param h This is the actual altitude.
   * @param params The parameters that define the algorithm such as control gains.
   * @param Ts The sampling period in seconds.
   * @return The throttle value saturated between 0 and the parameter of max throttle.
   */
  float total_energy_throttle(float va_c, float va, float h_c, float h,
                              const struct params_s & params);

  /**
   * This uses the error in the balance of energy to find the necessary elevator deflection to acheive that energy.
   * @param va_c This is the commanded airspeed.
   * @param va This is the actual airspeed.
   * @param h_c This is the commanded altitude.
   * @param h This is the actual altitude.
   * @param params The parameters that define the algorithm such as control gains.
   * @param Ts The sampling period in seconds.
   * @return The pitch command value saturated between min and max pitch.
   */
  float total_energy_pitch(float va_c, float va, float h_c, float h,
                           const struct params_s & params);

  /**
   * This calculates and updates the kinetic energy reference and error, the potential energy error.
   * @param va_c This is the commanded airspeed.
   * @param va This is the actual airspeed.
   * @param h_c This is the commanded altitude.
   * @param h This is the actual altitude.
   * @param params The parameters that define the algorithm such as control gains.
   */
  void update_energies(float va_c, float va, float h_c, float h, const struct params_s & params);

  /**
   * This is the integral value for the error in the total energy.
   */
  float E_integrator_;

  /**
   * This is the integral value for the error in the balance of energy.
   */
  float L_integrator_;

  /**
   * This is the current reference (desired) kinetic energy.
   */
  float K_ref;

  /**
   * This is the current error in the kinetic energy.
   */
  float K_error;

  /**
   * This is the current error in the potential energy.
   */
  float U_error;

  /**
   * The previous error in the energy balance.
   */
  float L_error_prev_;

  /**
   * The previous error in the total energy.
   */
  float E_error_prev_;
};
} // namespace rosplane

#endif //BUILD_CONTROLLER_TOTAL_ENERGY_H
