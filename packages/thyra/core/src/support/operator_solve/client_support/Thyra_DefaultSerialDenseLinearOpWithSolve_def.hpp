// @HEADER
// ***********************************************************************
// 
//    Thyra: Interfaces and Support for Abstract Numerical Algorithms
//                 Copyright (2004) Sandia Corporation
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
// Questions? Contact Michael A. Heroux (maherou@sandia.gov) 
// 
// ***********************************************************************
// @HEADER


#ifndef THYRA_DEFAULT_SERIAL_DENSE_LINEAR_OP_WITH_SOLVE_HPP
#define THYRA_DEFAULT_SERIAL_DENSE_LINEAR_OP_WITH_SOLVE_HPP


#include "Thyra_DefaultSerialDenseLinearOpWithSolve_decl.hpp"
#include "Thyra_LinearOpWithSolveBase.hpp"
#include "Thyra_DetachedMultiVectorView.hpp"
#include "Thyra_MultiVectorStdOps.hpp"
#include "Thyra_AssertOp.hpp"
#include "Teuchos_Assert.hpp"


namespace Thyra {

  
// Constructors/initializers/accessors


template<class Scalar>
DefaultSerialDenseLinearOpWithSolve<Scalar>::DefaultSerialDenseLinearOpWithSolve()
{}


template<class Scalar>
void DefaultSerialDenseLinearOpWithSolve<Scalar>::initialize(
  const RCP<const MultiVectorBase<Scalar> > &M )
{
  using Teuchos::outArg;
#ifdef TEUCHOS_DEBUG
  TEUCHOS_ASSERT(!is_null(M));
  TEUCHOS_ASSERT(isFullyInitialized(*M));
  TEUCHOS_ASSERT(M->range()->hasInCoreView());
  TEUCHOS_ASSERT(M->domain()->hasInCoreView());
  THYRA_ASSERT_VEC_SPACES("", *M->range(), *M->domain());
#endif
  factorize(*M, outArg(LU_), outArg(ipiv_));
  M_ = M;
}

template<class Scalar>
RCP<const LinearOpBase<Scalar> > DefaultSerialDenseLinearOpWithSolve<Scalar>::getFwdOp() const
{
  return M_;
}

// Overridden from LinearOpBase


template<class Scalar>
RCP<const VectorSpaceBase<Scalar> >
DefaultSerialDenseLinearOpWithSolve<Scalar>::range() const
{
  if (!is_null(M_))
    return M_->range();
  return Teuchos::null;
}


template<class Scalar>
RCP<const VectorSpaceBase<Scalar> >
DefaultSerialDenseLinearOpWithSolve<Scalar>::domain() const
{
  if (!is_null(M_))
    return M_->domain();
  return Teuchos::null;
}


// protected


// Overridden from LinearOpBase


template<class Scalar>
bool DefaultSerialDenseLinearOpWithSolve<Scalar>::opSupportedImpl(
  EOpTransp M_trans) const
{
  return Thyra::opSupported(*M_, M_trans);
}


template<class Scalar>
void DefaultSerialDenseLinearOpWithSolve<Scalar>::applyImpl(
  const EOpTransp M_trans,
  const MultiVectorBase<Scalar> &X,
  const Ptr<MultiVectorBase<Scalar> > &Y,
  const Scalar alpha,
  const Scalar beta
  ) const
{
  Thyra::apply( *M_, M_trans, X, Y, alpha, beta );
}


// Overridden from LinearOpWithSolveBase


template<class Scalar>
bool DefaultSerialDenseLinearOpWithSolve<Scalar>::solveSupportsImpl(
  EOpTransp M_trans) const
{
  typedef Teuchos::ScalarTraits<Scalar> ST;
  return ( ST::isComplex ? ( M_trans!=CONJ ) : true );
}


template<class Scalar>
bool DefaultSerialDenseLinearOpWithSolve<Scalar>::solveSupportsSolveMeasureTypeImpl(
  EOpTransp M_trans, const SolveMeasureType& solveMeasureType) const
{
  // We support all solve measures since we are a direct solver
  return this->solveSupportsImpl(M_trans);
}


template<class Scalar>
SolveStatus<Scalar>
DefaultSerialDenseLinearOpWithSolve<Scalar>::solveImpl(
  const EOpTransp M_trans,
  const MultiVectorBase<Scalar> &B,
  const Ptr<MultiVectorBase<Scalar> > &X,
  const Ptr<const SolveCriteria<Scalar> > solveCriteria
  ) const
{
#ifdef TEUCHOS_DEBUG
  THYRA_ASSERT_LINEAR_OP_MULTIVEC_APPLY_SPACES(
    "DefaultSerialDenseLinearOpWithSolve<Scalar>::solve(...)",
    *this, M_trans, *X, &B );
#endif
  backsolve( LU_, ipiv_, M_trans, B, X );
  SolveStatus<Scalar> solveStatus;
  solveStatus.solveStatus = SOLVE_STATUS_CONVERGED;\
  return solveStatus;
}


// private


template<class Scalar>
void DefaultSerialDenseLinearOpWithSolve<Scalar>::factorize(
  const MultiVectorBase<Scalar> &M,
  const Ptr<RTOpPack::ConstSubMultiVectorView<Scalar> > &LU,
  const Ptr<Array<int> > &ipiv
  )
{
  using Teuchos::outArg;
  const ConstDetachedMultiVectorView<Scalar> dM(M);
  const int dim = dM.subDim();
  ipiv->resize(dim);
  RTOpPack::SubMultiVectorView<Scalar> LU_tmp(dim, dim);
  RTOpPack::assign_entries<Scalar>( outArg(LU_tmp), dM.smv() );
  int rank = -1;
  RTOpPack::getrf<Scalar>( LU_tmp, (*ipiv)(), outArg(rank) );
  TEUCHOS_ASSERT_EQUALITY( dim, rank );
  *LU = LU_tmp; // Weak copy
}


template<class Scalar>
void DefaultSerialDenseLinearOpWithSolve<Scalar>::backsolve(
  const RTOpPack::ConstSubMultiVectorView<Scalar> &LU,
  const ArrayView<const int> ipiv,
  const EOpTransp transp,
  const MultiVectorBase<Scalar> &B,
  const Ptr<MultiVectorBase<Scalar> > &X
  )
{
  using Teuchos::outArg;
  assign( X, B );
  DetachedMultiVectorView<Scalar> dX(*X);
  RTOpPack::getrs<Scalar>( LU, ipiv, convertToRTOpPackETransp(transp),
    outArg(dX.smv()) );
}


}	// end namespace Thyra


#endif	// THYRA_DEFAULT_SERIAL_DENSE_LINEAR_OP_WITH_SOLVE_HPP
