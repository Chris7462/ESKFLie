/**
 * \file se2_localization_liekf.cpp
 *
 *  Created on: October 04, 2024
 *     \author: Yi-Chen Zhang
 *
 *  ---------------------------------------------------------
 *  Demonstration example:
 *
 *  2D Robot localization based on position measurements (GPS-like)
 *  using the (Left) Invariant Extended Kalman Filter method.
 *
 *  See se2_localization.cpp for the right invariant equivalent
 *  ---------------------------------------------------------
 *
 *  We consider a robot in the plane.
 *  The robot receives control actions in the form of axial
 *  and angular velocities, and is able to measure its position
 *  using a GPS for instance.
 *
 *  The robot pose X is in SE(2) and the GPS measurement y_k is in R^2,
 *
 *          | cos th  -sin th   x |
 *      X = | sin th   cos th   y |  // position and orientation
 *          |   0        0      1 |
 *
 *  The control signal u is a twist in se(2) comprising longitudinal
 *  velocity v and angular velocity w, with no lateral velocity
 *  component, integrated over the sampling time dt.
 *
 *      u = (v*dt, 0, w*dt)
 *
 *  The control is corrupted by additive Gaussian noise u_noise,
 *  with covariance
 *
 *    Q = diagonal(sigma_v^2, sigma_s^2, sigma_w^2).
 *
 *  This noise accounts for possible lateral slippage u_s
 *  through a non-zero value of sigma_s,
 *
 *  At the arrival of a control u, the robot pose is updated
 *  with X <-- X * Exp(u) = X + u.
 *
 *  GPS measurements are put in Cartesian form for simplicity.
 *  Their noise n is zero mean Gaussian, and is specified
 *  with a covariances matrix R.
 *  We notice the rigid motion action y = h(X) = X * [0 0 1]^T + v
 *
 *      y_k = (x, y)       // robot coordinates
 *
 *  We define the pose to estimate as X in SE(2).
 *  The estimation error dx and its covariance P are expressed
 *  in the global space at epsilon.
 *
 *  All these variables are summarized again as follows
 *
 *    X   : robot pose, SE(2)
 *    u   : robot control, (v*dt ; 0 ; w*dt) in se(2)
 *    Q   : control perturbation covariance
 *    y   : robot position measurement in global frame, R^2
 *    R   : covariance of the measurement noise
 *
 *  The motion and measurement models are
 *
 *    X_(t+1) = f(X_t, u) = X_t * Exp ( u )     // motion equation
 *    y_k     = h(X, b_k) = X * [0 0 1]^T       // measurement equation
 *
 *  The algorithm below comprises first a simulator to
 *  produce measurements, then uses these measurements
 *  to estimate the state, using a Lie-based
 *  Left Invariant Kalman filter.
 *
 *  Printing simulated state and estimated state together
 *  with an unfiltered state (i.e. without Kalman corrections)
 *  allows for evaluating the quality of the estimates.
 */

#include "manif/SE2.h"

#include <vector>

#include <iostream>
#include <iomanip>

using std::cout;
using std::endl;

using namespace Eigen;

typedef Array<double, 2, 1> Array2d;
typedef Array<double, 3, 1> Array3d;

int main()
{
  std::srand((unsigned int) time(0));

  // START CONFIGURATION
  //
  //
  const Matrix3d I = Matrix3d::Identity();

  // Define the robot pose element and its covariance
  manif::SE2d X, X_simulation, X_unfiltered;
  Matrix3d    P;

  X_simulation.setIdentity();
  X.setIdentity();
  X_unfiltered.setIdentity();
  P.setZero();

  // Define a control vector and its noise and covariance
  manif::SE2Tangentd  u_simu, u_est, u_unfilt;
  Vector3d            u_nom, u_noisy, u_noise;
  Array3d             u_sigmas;
  Matrix3d            U;

  u_nom    << 0.1, 0.0, 0.1;
  u_sigmas << 0.1, 0.1, 0.05;
  U        = (u_sigmas * u_sigmas).matrix().asDiagonal();

  // Declare the Jacobians of the motion wrt robot and control
  manif::SE2d::Jacobian J_x, J_u;

  // Define the gps measurements in R^2
  Vector2d    y, y_noise;
  Array2d     y_sigmas;
  Matrix2d    R;

  y_sigmas << 0.01, 0.01;
  R        = (y_sigmas * y_sigmas).matrix().asDiagonal();

  // Declare the Jacobian of the measurements wrt the robot pose
  Matrix<double, 2, 3>    H;      // H = J_e_x

  // Declare some temporaries
  Vector2d                e, z;   // expectation, innovation
  Matrix2d                E, Z;   // covariances of the above
  Matrix2d                M;
  Matrix<double, 3, 2>    K;      // Kalman gain
  manif::SE2Tangentd      dx;     // optimal update step, or error-state

  //
  //
  // CONFIGURATION DONE



  // DEBUG
  cout << std::fixed   << std::setprecision(3) << std::showpos << endl;
  cout << "X STATE     :    X      Y    THETA" << endl;
  cout << "----------------------------------" << endl;
  cout << "X initial   : " << X_simulation.log().coeffs().transpose() << endl;
  cout << "----------------------------------" << endl;
  // END DEBUG




  // START TEMPORAL LOOP
  //
  //

  // Make 10 steps. Measure one GPS position each time.
  for (int t = 0; t < 1000; t++) {
    //// I. Simulation ###############################################################################

    /// simulate noise
    u_noise = u_sigmas * Array3d::Random();             // control noise
    u_noisy = u_nom + u_noise;                          // noisy control

    u_simu   = u_nom;
    u_est    = u_noisy;
    u_unfilt = u_noisy;

    /// first we move - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    X_simulation = X_simulation + u_simu;               // overloaded X.rplus(u) = X * exp(u)

    /// then we receive noisy gps measurement - - - - - - - - - - - - - - - -
    y_noise = y_sigmas * Array2d::Random();             // simulate measurement noise

    y = X_simulation.translation();                     // position measurement, before adding noise
    y = y + y_noise;                                    // position measurement, noisy




    //// II. Estimation ###############################################################################

    /// First we move - - - - - - - - - - - - - - - - - - - - - - - - - - - -

    X = X.plus(u_est, J_x, J_u);                        // X * exp(u), with Jacobians

    P = J_x * P * J_x.transpose() + J_u * U * J_u.transpose();


    /// Then we correct using the gps position - - - - - - - - - - - - - - -

    // expectation
    e = X.translation();                                // e = t, for X = (R,t).
    H.topLeftCorner<2, 2>() = Matrix2d::Identity();
    H.topRightCorner<2, 1>() = Vector2d::Zero();
    E = H * P * H.transpose();

    // innovation
    M = X.inverse().rotation();
    z = M * (y - e);                                    // z = X.inv().act(y) - X.inv().act(e)
                                                        //   = M * (y - e)
    Z = E + M * R * M.transpose();

    // Kalman gain
    K = P * H.transpose() * Z.inverse();

    // Correction step
    dx = K * z;

    // Update
    X = X.plus(dx);
    P = (I - K * H) * P;




    //// III. Unfiltered ##############################################################################

    // move also an unfiltered version for comparison purposes
    X_unfiltered = X_unfiltered + u_unfilt;




    //// IV. Results ##############################################################################

    // DEBUG
    cout << "X simulated : " << X_simulation.log().coeffs().transpose() << endl;
    cout << "X estimated : " << X.log().coeffs().transpose() << endl;
    cout << "X unfilterd : " << X_unfiltered.log().coeffs().transpose() << endl;
    cout << "----------------------------------" << endl;
    // END DEBUG

  }

  //
  //
  // END OF TEMPORAL LOOP. DONE.

  return 0;
}