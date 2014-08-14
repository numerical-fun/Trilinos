// @HEADER
// ************************************************************************
//
//               Rapid Optimization Library (ROL) Package
//                 Copyright (2014) Sandia Corporation
//
// Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
// license for use of this work by or on behalf of the U.S. Government.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact lead developers:
//              Drew Kouri   (dpkouri@sandia.gov) and
//              Denis Ridzal (dridzal@sandia.gov)
//
// ************************************************************************
// @HEADER

#ifndef ROL_EQUALITY_CONSTRAINT_SIMOPT_H
#define ROL_EQUALITY_CONSTRAINT_SIMOPT_H

#include "ROL_EqualityConstraint.hpp"
#include "ROL_Vector_SimOpt.hpp"
#include "ROL_Types.hpp"
#include <iostream>

/** @ingroup func_group
    \class ROL::EqualityConstraint_SimOpt
    \brief Defines the equality constraint operator interface for simulation-based optimization.

    This equality constraint interface inherits from ROL_EqualityConstraint and implements the 
    situation when \f$\mathcal{X}=\mathcal{U}\times\mathcal{Z}\f$ where \f$\mathcal{U}\f$ and 
    \f$\mathcal{Z}\f$ are Banach spaces.  \f$\mathcal{U}\f$ denotes the simulation solution 
    space and \f$\mathcal{Z}\f$ denotes the design/control/inversion parameters. These constraints 
    are of the form
    \f[
      c(u,z) = 0 \,.
    \f]
    The basic operator interface, to be implemented by the user, requires:
    \li #value -- constraint evaluation.
    \li #applyJacobian_1        -- action of the partial constraint Jacobian --derivatives are 
                                   with respect to the first component \f$\mathcal{U}\f$;
    \li #applyJacobian_2        -- action of the partial constraint Jacobian --derivatives are 
                                   with respect to the second component \f$\mathcal{Z}\f$;
    \li #applyAdjointJacobian_1 -- action of the adjoint of the partial constraint Jacobian --derivatives are 
                                   with respect to the first component \f$\mathcal{U}\f$;
    \li #applyAdjointJacobian_2 -- action of the adjoint of the partial constraint Jacobian --derivatives are 
                                   with respect to the second component \f$\mathcal{Z}\f$;

    The user may also overload:
    \li #applyAdjointHessian_11  -- action of the adjoint of the partial constraint Hessian --derivatives 
                                    are with respect to the first component only;
    \li #applyAdjointHessian_12  -- action of the adjoint of the partial constraint Hessian --derivatives 
                                    are with respect to the first and second components;
    \li #applyAdjointHessian_21  -- action of the adjoint of the partial constraint Hessian --derivatives 
                                    are with respect to the second and first components;
    \li #applyAdjointHessian_22  -- action of the adjoint of the partial constraint Hessian --derivatives 
                                    are with respect to the second component only;
    \li #solveAugmentedSystem -- solution of the augmented system --the default is an iterative
                                 scheme based on the action of the Jacobian and its adjoint.
    \li #applyPreconditioner  -- action of a constraint preconditioner --the default is null-op.

    ---
*/


namespace ROL {

template <class Real>
class EqualityConstraint_SimOpt : public EqualityConstraint<Real> {
public:
  /** \brief Evaluate the constraint operator \f$c:\mathcal{U}\times\mathcal{Z} \rightarrow \mathcal{C}\f$
             at \f$(u,z)\f$.

             @param[out]      c   is the result of evaluating the constraint operator at @b \f$(u,z)\f$; a constraint-space vector
             @param[in]       u   is the constraint argument; an optimization-space vector
             @param[in]       z   is the constraint argument; an optimization-space vector
             @param[in,out]   tol is a tolerance for inexact evaluations; currently unused

             On return, \f$\mathsf{c} = c(u,z)\f$,
             where \f$\mathsf{c} \in \mathcal{C}\f$, \f$\mathsf{u} \in \mathcal{U}\f$, and $\f$\mathsf{z} \in\mathcal{Z}\f$.

             ---
  */
  virtual void value(Vector<Real> &c,
                     const Vector<Real> &u,
                     const Vector<Real> &z,
                     Real &tol) = 0;

  /** \brief Given \f$z\f$, solve \f$c(u,z)=0\f$ for \f$u\f$.

             @param[out]      u   is the solution vector
             @param[in]       z   is the constraint argument; an optimization-space vector
             @param[in,out]   tol is a tolerance for inexact evaluations; currently unused

             ---
  */
  virtual void solve(Vector<Real> &u, 
                     const Vector<Real> &z,
                     Real &tol) {
    u.zero();
  }


  /** \brief Apply the partial constraint Jacobian at \f$(u,z)\f$, 
             \f$c_u(u,z) \in L(\mathcal{U}, \mathcal{C})\f$,
             to the vector \f$v\f$.

             @param[out]      jv  is the result of applying the constraint Jacobian to @b v at @b \f$(u,z)\f$; a constraint-space vector
             @param[in]       v   is an optimization-space vector
             @param[in]       u   is the constraint argument; an optimization-space vector
             @param[in]       z   is the constraint argument; an optimization-space vector
             @param[in,out]   tol is a tolerance for inexact evaluations; currently unused

             On return, \f$\mathsf{jv} = c_u(u,z)v\f$, where
             \f$v \in \mathcal{U}\f$, \f$\mathsf{jv} \in \mathcal{C}\f$.

             ---
  */
  virtual void applyJacobian_1(Vector<Real> &jv,
                               const Vector<Real> &v,
                               const Vector<Real> &u,
                               const Vector<Real> &z,
                               Real &tol) = 0;


  /** \brief Apply the partial constraint Jacobian at \f$(u,z)\f$, 
             \f$c_z(u,z) \in L(\mathcal{Z}, \mathcal{C})\f$,
             to the vector \f$v\f$.

             @param[out]      jv  is the result of applying the constraint Jacobian to @b v at @b \f$(u,z)\f$; a constraint-space vector
             @param[in]       v   is an optimization-space vector
             @param[in]       u   is the constraint argument; an optimization-space vector
             @param[in]       z   is the constraint argument; an optimization-space vector
             @param[in,out]   tol is a tolerance for inexact evaluations; currently unused

             On return, \f$\mathsf{jv} = c_z(u,z)v\f$, where
             \f$v \in \mathcal{Z}\f$, \f$\mathsf{jv} \in \mathcal{C}\f$. 

             ---
  */
  virtual void applyJacobian_2(Vector<Real> &jv,
                               const Vector<Real> &v,
                               const Vector<Real> &u,
                               const Vector<Real> &z,
                               Real &tol) = 0; 


  /** \brief Apply the inverse partial constraint Jacobian at \f$(u,z)\f$, 
             \f$c_u(u,z)^{-1} \in L(\mathcal{C}, \mathcal{U})\f$,
             to the vector \f$v\f$.

             @param[out]      ijv is the result of applying the inverse constraint Jacobian to @b v at @b \f$(u,z)\f$; an optimization-space vector
             @param[in]       v   is a constraint-space vector
             @param[in]       u   is the constraint argument; an optimization-space vector
             @param[in]       z   is the constraint argument; an optimization-space vector
             @param[in,out]   tol is a tolerance for inexact evaluations; currently unused

             On return, \f$\mathsf{ijv} = c_u(u,z)^{-1}v\f$, where
             \f$v \in \mathcal{C}\f$, \f$\mathsf{ijv} \in \mathcal{U}\f$.

             ---
  */
  virtual void applyInverseJacobian_1(Vector<Real> &ijv,
                                      const Vector<Real> &v,
                                      const Vector<Real> &u,
                                      const Vector<Real> &z,
                                      Real &tol) = 0;


  /** \brief Apply the adjoint of the partial constraint Jacobian at \f$(u,z)\f$, 
             \f$c_u(u,z)^* \in L(\mathcal{C}^*, \mathcal{U}^*)\f$,
             to the vector \f$v\f$.

             @param[out]      ajv is the result of applying the adjoint of the constraint Jacobian to @b v at @b (u,z); a dual optimization-space vector
             @param[in]       v   is a dual constraint-space vector
             @param[in]       u   is the constraint argument; an optimization-space vector
             @param[in]       z   is the constraint argument; an optimization-space vector
             @param[in,out]   tol is a tolerance for inexact evaluations; currently unused

             On return, \f$\mathsf{ajv} = c_u(u,z)^*v\f$, where
             \f$v \in \mathcal{C}^*\f$, \f$\mathsf{ajv} \in \mathcal{U}^*\f$.

             ---
  */
  virtual void applyAdjointJacobian_1(Vector<Real> &ajv,
                                      const Vector<Real> &v,
                                      const Vector<Real> &u,
                                      const Vector<Real> &z,
                                      Real &tol) = 0;


  /** \brief Apply the adjoint of the partial constraint Jacobian at \f$(u,z)\f$, 
             \f$c_z(u,z)^* \in L(\mathcal{C}^*, \mathcal{Z}^*)\f$,
             to vector \f$v\f$.

             @param[out]      ajv is the result of applying the adjoint of the constraint Jacobian to @b v at @b \f$(u,z)\f$; a dual optimization-space vector
             @param[in]       v   is a dual constraint-space vector
             @param[in]       u   is the constraint argument; an optimization-space vector
             @param[in]       z   is the constraint argument; an optimization-space vector
             @param[in,out]   tol is a tolerance for inexact evaluations; currently unused

             On return, \f$\mathsf{ajv} = c_z(u,z)^*v\f$, where
             \f$v \in \mathcal{C}^*\f$, \f$\mathsf{ajv} \in \mathcal{Z}^*\f$.

             ---
  */
  virtual void applyAdjointJacobian_2(Vector<Real> &ajv,
                                      const Vector<Real> &v,
                                      const Vector<Real> &u,
                                      const Vector<Real> &z,
                                      Real &tol) = 0;


  /** \brief Apply the inverse of the adjoint of the partial constraint Jacobian at \f$(u,z)\f$, 
             \f$c_u(u,z)^{-*} \in L(\mathcal{U}^*, \mathcal{C}^*)\f$,
             to the vector \f$v\f$.

             @param[out]      iajv is the result of applying the inverse adjoint of the constraint Jacobian to @b v at @b (u,z); a dual constraint-space vector
             @param[in]       v   is a dual optimization-space vector
             @param[in]       u   is the constraint argument; an optimization-space vector
             @param[in]       z   is the constraint argument; an optimization-space vector
             @param[in,out]   tol is a tolerance for inexact evaluations; currently unused

             On return, \f$\mathsf{iajv} = c_u(u,z)^{-*}v\f$, where
             \f$v \in \mathcal{U}^*\f$, \f$\mathsf{iajv} \in \mathcal{C}^*\f$.

             ---
  */
  virtual void applyInverseAdjointJacobian_1(Vector<Real> &iajv,
                                             const Vector<Real> &v,
                                             const Vector<Real> &u,
                                             const Vector<Real> &z,
                                             Real &tol) {
    iajv.zero();
  };


  /** \brief Apply the adjoint of the partial constraint Hessian at \f$(u,z)\f$,
             \f$c_{uu}(u,z)^* \in L(L(\mathcal{C}^*, \mathcal{U}^*), \mathcal{U}^*)\f$,
             to vector \f$v\f$ in direction \f$w\f$.

             @param[out]      ahwv is the result of applying the adjoint of the constraint Hessian to @b v at @b \f$(u,z)\f$ in direction @b w; a dual optimization-space vector
             @param[in]       w    is the direction vector; a dual constraint-space vector
             @param[in]       v    is a dual optimization-space vector
             @param[in]       u    is the constraint argument; an optimization-space vector
             @param[in]       z    is the constraint argument; an optimization-space vector
             @param[in,out]   tol  is a tolerance for inexact evaluations; currently unused

             On return, \f$ \mathsf{ahwv} = c_{uu}(u,z)^*(w,v) \f$, where
             \f$w \in \mathcal{C}^*\f$, \f$v \in \mathcal{U}^*\f$, and \f$\mathsf{ahwv} \in \mathcal{U}^*\f$.

             ---
  */
  virtual void applyAdjointHessian_11(Vector<Real> &ahwv,
                                      const Vector<Real> &w,
                                      const Vector<Real> &v,
                                      const Vector<Real> &u,
                                      const Vector<Real> &z,
                                      Real &tol) = 0;


  /** \brief Apply the adjoint of the partial constraint Hessian at \f$(u,z)\f$,
             \f$c_{uz}(u,z)^* \in L(L(\mathcal{C}^*, \mathcal{U}^*), \mathcal{Z}^*)\f$,
             to vector \f$v\f$ in direction \f$w\f$.

             @param[out]      ahwv is the result of applying the adjoint of the constraint Hessian to @b v at @b \f$(u,z)\f$ in direction @b w; a dual optimization-space vector
             @param[in]       w    is the direction vector; a dual constraint-space vector
             @param[in]       v    is a dual optimization-space vector
             @param[in]       u    is the constraint argument; an optimization-space vector
             @param[in]       z    is the constraint argument; an optimization-space vector
             @param[in,out]   tol  is a tolerance for inexact evaluations; currently unused

             On return, \f$ \mathsf{ahwv} = c_{uz}(u,z)^*(w,v) \f$, where
             \f$w \in \mathcal{C}^*\f$, \f$v \in \mathcal{U}^*\f$, and \f$\mathsf{ahwv} \in \mathcal{Z}^*\f$.

             ---
  */
  virtual void applyAdjointHessian_12(Vector<Real> &ahwv,
                                      const Vector<Real> &w,
                                      const Vector<Real> &v,
                                      const Vector<Real> &u,
                                      const Vector<Real> &z,
                                      Real &tol) = 0;


  /** \brief Apply the adjoint of the partial constraint Hessian at \f$(u,z)\f$,
             \f$c_{zu}(u,z)^* \in L(L(\mathcal{C}^*, \mathcal{Z}^*), \mathcal{U}^*)\f$,
             to vector \f$v\f$ in direction \f$w\f$.

             @param[out]      ahwv is the result of applying the adjoint of the constraint Hessian to @b v at @b \f$(u,z)\f$ in direction @b w; a dual optimization-space vector
             @param[in]       w    is the direction vector; a dual constraint-space vector
             @param[in]       v    is a dual optimization-space vector
             @param[in]       u    is the constraint argument; an optimization-space vector
             @param[in]       z    is the constraint argument; an optimization-space vector
             @param[in,out]   tol  is a tolerance for inexact evaluations; currently unused

             On return, \f$ \mathsf{ahwv} = c_{zu}(u,z)^*(w,v) \f$, where
             \f$w \in \mathcal{C}^*\f$, \f$v \in \mathcal{Z}^*\f$, and \f$\mathsf{ahwv} \in \mathcal{U}^*\f$.

             ---
  */
  virtual void applyAdjointHessian_21(Vector<Real> &ahwv,
                                      const Vector<Real> &w,
                                      const Vector<Real> &v,
                                      const Vector<Real> &u,
                                      const Vector<Real> &z,
                                      Real &tol) = 0;


  /** \brief Apply the adjoint of the partial constraint Hessian at \f$(u,z)\f$,
             \f$c_{zz}(u,z)^* \in L(L(\mathcal{C}^*, \mathcal{Z}^*), \mathcal{Z}^*)\f$,
             to vector \f$v\f$ in direction \f$w\f$.

             @param[out]      ahwv is the result of applying the adjoint of the constraint Hessian to @b v at @b \f$(u,z)\f$ in direction @b w; a dual optimization-space vector
             @param[in]       w    is the direction vector; a dual constraint-space vector
             @param[in]       v    is a dual optimization-space vector
             @param[in]       u    is the constraint argument; an optimization-space vector
             @param[in]       z    is the constraint argument; an optimization-space vector
             @param[in,out]   tol  is a tolerance for inexact evaluations; currently unused

             On return, \f$ \mathsf{ahwv} = c_{zz}(u,z)^*(w,v) \f$, where
             \f$w \in \mathcal{C}^*\f$, \f$v \in \mathcal{Z}^*\f$, and \f$\mathsf{ahwv} \in \mathcal{Z}^*\f$. 

             ---
  */
  virtual void applyAdjointHessian_22(Vector<Real> &ahwv,
                                      const Vector<Real> &w,
                                      const Vector<Real> &v,
                                      const Vector<Real> &u,
                                      const Vector<Real> &z,
                                      Real &tol) = 0;


  /** \brief Approximately solves the <em> augmented system </em>
             \f[
                 \begin{pmatrix}
                   I     & c'(x)^* \\
                   c'(x) & 0
                 \end{pmatrix}
                 \begin{pmatrix}
                   v_{1} \\
                   v_{2}
                 \end{pmatrix}
                 =
                 \begin{pmatrix}
                   b_{1} \\
                   b_{2}
                 \end{pmatrix}
             \f]
             where \f$v_{1} \in \mathcal{X}\f$, \f$v_{2} \in \mathcal{C}^*\f$,
             \f$b_{1} \in \mathcal{X}^*\f$, \f$b_{2} \in \mathcal{C}\f$,
             \f$I : \mathcal{X} \rightarrow \mathcal{X}^*\f$ is an identity
             operator, and \f$0 : \mathcal{C}^* \rightarrow \mathcal{C}\f$
             is a zero operator.

             @param[out]      v1  is the optimization-space component of the result
             @param[out]      v2  is the dual constraint-space component of the result
             @param[in]       b1  is the dual optimization-space component of the right-hand side
             @param[in]       b2  is the constraint-space component of the right-hand side
             @param[in]       x   is the constraint argument; an optimization-space vector
             @param[in,out]   tol is the nominal relative residual tolerance

             On return, \f$ [\mathsf{v1} \,\, \mathsf{v2}] \f$ approximately
             solves the augmented system, where the size of the residual is
             governed by special stopping conditions. \n\n
             The default implementation is the preconditioned generalized
             minimal residual (GMRES) method, which enables the use of
             nonsymmetric preconditioners.

             ---
  */
  virtual void solveAugmentedSystem(Vector<Real> &v1,
                                    Vector<Real> &v2,
                                    const Vector<Real> &b1,
                                    const Vector<Real> &b2,
                                    const Vector<Real> &x,
                                    Real &tol) {
    EqualityConstraint<Real>::solveAugmentedSystem(v1,v2,b1,b2,x,tol);
  }


  /** \brief Apply a constraint preconditioner at \f$x\f$, \f$P(x) \in L(\mathcal{C}, \mathcal{C})\f$,
             to vector \f$v\f$.  In general, this preconditioner satisfies the following relationship:
             \f[
               c'(x) c'(x)^* P(x) v \approx v \,.
             \f]
             It is used by the #solveAugmentedSystem method.

             @param[out]      pv  is the result of applying the constraint preconditioner to @b v at @b x; a constraint-space vector
             @param[in]       v   is a constraint-space vector
             @param[in]       x   is the preconditioner argument; an optimization-space vector
             @param[in,out]   tol is a tolerance for inexact evaluations

             On return, \f$\mathsf{pv} = P(x)v\f$, where
             \f$v \in \mathcal{C}\f$, \f$\mathsf{pv} \in \mathcal{C}\f$. \n\n
             The default implementation is a null-op.

             ---
  */
  virtual void applyPreconditioner(Vector<Real> &pv,
                                   const Vector<Real> &v,
                                   const Vector<Real> &x,
                                   Real &tol) {}


  EqualityConstraint_SimOpt(void) : EqualityConstraint<Real>() {}

  /** \brief Update constraint functions.  
                x is the optimization variable, 
                flag = true if optimization variable is changed,
                iter is the outer algorithm iterations count.
  */
  virtual void update( const Vector<Real> &x, bool flag = true, int iter = -1 ) {}

  /** \brief Check if the vector, v, is feasible
  */
  virtual bool isFeasible( const Vector<Real> &v ) { return true; }

  virtual void value(Vector<Real> &c,
                     const Vector<Real> &x,
                     Real &tol) {
    const Vector_SimOpt<Real> &xs = Teuchos::dyn_cast<const Vector_SimOpt<Real> >(
      Teuchos::dyn_cast<const Vector<Real> >(x));
    this->value(c,*(xs.get_1()),*(xs.get_2()),tol);
  }


  virtual void applyJacobian(Vector<Real> &jv,
                             const Vector<Real> &v,
                             const Vector<Real> &x,
                             Real &tol) { 
    const Vector_SimOpt<Real> &xs = Teuchos::dyn_cast<const Vector_SimOpt<Real> >(
      Teuchos::dyn_cast<const Vector<Real> >(x));
    const Vector_SimOpt<Real> &vs = Teuchos::dyn_cast<const Vector_SimOpt<Real> >(
      Teuchos::dyn_cast<const Vector<Real> >(v));
    this->applyJacobian_1(jv,*(vs.get_1()),*(xs.get_1()),*(xs.get_2()),tol);
    Teuchos::RCP<Vector<Real> > jv2 = jv.clone();
    this->applyJacobian_2(*jv2,*(vs.get_2()),*(xs.get_1()),*(xs.get_2()),tol);
    jv.plus(*jv2);
  }


  virtual void applyAdjointJacobian(Vector<Real> &ajv,
                                    const Vector<Real> &v,
                                    const Vector<Real> &x,
                                    Real &tol) { 
    Vector_SimOpt<Real> &ajvs = Teuchos::dyn_cast<Vector_SimOpt<Real> >(
      Teuchos::dyn_cast<Vector<Real> >(ajv));
    const Vector_SimOpt<Real> &xs = Teuchos::dyn_cast<const Vector_SimOpt<Real> >(
      Teuchos::dyn_cast<const Vector<Real> >(x));
    Teuchos::RCP<Vector<Real> > ajv1 = (ajvs.get_1())->clone();
    this->applyAdjointJacobian_1(*ajv1,v,*(xs.get_1()),*(xs.get_2()),tol);
    ajvs.set_1(*ajv1);
    Teuchos::RCP<Vector<Real> > ajv2 = (ajvs.get_2())->clone();
    this->applyAdjointJacobian_2(*ajv2,v,*(xs.get_1()),*(xs.get_2()),tol);
    ajvs.set_2(*ajv2);
  }


  virtual void applyAdjointHessian(Vector<Real> &ahwv,
                                   const Vector<Real> &w,
                                   const Vector<Real> &v,
                                   const Vector<Real> &x,
                                   Real &tol) {
    Vector_SimOpt<Real> &ahwvs = Teuchos::dyn_cast<Vector_SimOpt<Real> >(
      Teuchos::dyn_cast<Vector<Real> >(ahwv));
    const Vector_SimOpt<Real> &xs = Teuchos::dyn_cast<const Vector_SimOpt<Real> >(
      Teuchos::dyn_cast<const Vector<Real> >(x));
    const Vector_SimOpt<Real> &vs = Teuchos::dyn_cast<const Vector_SimOpt<Real> >(
      Teuchos::dyn_cast<const Vector<Real> >(v));
    // Block-row 1
    Teuchos::RCP<Vector<Real> > C11 = (ahwvs.get_1())->clone();
    Teuchos::RCP<Vector<Real> > C21 = (ahwvs.get_1())->clone();
    this->applyAdjointHessian_11(*C11,w,*(vs.get_1()),*(xs.get_1()),*(xs.get_2()),tol);
    this->applyAdjointHessian_21(*C21,w,*(vs.get_2()),*(xs.get_1()),*(xs.get_2()),tol);
    C11->plus(*C21);
    ahwvs.set_1(*C11); 
    // Block-row 2
    Teuchos::RCP<Vector<Real> > C12 = (ahwvs.get_2())->clone();
    Teuchos::RCP<Vector<Real> > C22 = (ahwvs.get_2())->clone();
    this->applyAdjointHessian_12(*C12,w,*(vs.get_1()),*(xs.get_1()),*(xs.get_2()),tol);
    this->applyAdjointHessian_22(*C22,w,*(vs.get_2()),*(xs.get_1()),*(xs.get_2()),tol);
    C22->plus(*C12);
    ahwvs.set_2(*C22); 
  }


}; // class EqualityConstraint_SimOpt

} // namespace ROL

#endif
