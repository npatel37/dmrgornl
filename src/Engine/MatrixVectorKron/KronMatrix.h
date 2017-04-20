/*
Copyright (c) 2012-2017, UT-Battelle, LLC
All rights reserved

[DMRG++, Version 4.]
[by G.A., Oak Ridge National Laboratory]

UT Battelle Open Source Software License 11242008

OPEN SOURCE LICENSE

Subject to the conditions of this License, each
contributor to this software hereby grants, free of
charge, to any person obtaining a copy of this software
and associated documentation files (the "Software"), a
perpetual, worldwide, non-exclusive, no-charge,
royalty-free, irrevocable copyright license to use, copy,
modify, merge, publish, distribute, and/or sublicense
copies of the Software.

1. Redistributions of Software must retain the above
copyright and license notices, this list of conditions,
and the following disclaimer.  Changes or modifications
to, or derivative works of, the Software should be noted
with comments and the contributor and organization's
name.

2. Neither the names of UT-Battelle, LLC or the
Department of Energy nor the names of the Software
contributors may be used to endorse or promote products
derived from this software without specific prior written
permission of UT-Battelle.

3. The software and the end-user documentation included
with the redistribution, with or without modification,
must include the following acknowledgment:

"This product includes software produced by UT-Battelle,
LLC under Contract No. DE-AC05-00OR22725  with the
Department of Energy."

*********************************************************
DISCLAIMER

THE SOFTWARE IS SUPPLIED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT OWNER, CONTRIBUTORS, UNITED STATES GOVERNMENT,
OR THE UNITED STATES DEPARTMENT OF ENERGY BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

NEITHER THE UNITED STATES GOVERNMENT, NOR THE UNITED
STATES DEPARTMENT OF ENERGY, NOR THE COPYRIGHT OWNER, NOR
ANY OF THEIR EMPLOYEES, REPRESENTS THAT THE USE OF ANY
INFORMATION, DATA, APPARATUS, PRODUCT, OR PROCESS
DISCLOSED WOULD NOT INFRINGE PRIVATELY OWNED RIGHTS.

*********************************************************

*/
/** \ingroup DMRG */
/*@{*/

/*! \file KronMatrix.h
 *
 *
 */

#ifndef KRON_MATRIX_HEADER_H
#define KRON_MATRIX_HEADER_H

#include "Matrix.h"
#include "KronConnections.h"
#include "Concurrency.h"
#include "Parallelizer.h"

namespace Dmrg {

template<typename ModelType,typename ModelHelperType_>
class KronMatrix {

	typedef InitKron<ModelType, ModelHelperType_> InitKronType;
	typedef typename InitKronType::SparseMatrixType SparseMatrixType;
	typedef typename SparseMatrixType::value_type ComplexOrRealType;
	typedef KronConnections<ModelType, ModelHelperType_> KronConnectionsType;
	typedef typename KronConnectionsType::MatrixType MatrixType;
	typedef typename KronConnectionsType::VectorType VectorType;
	typedef typename InitKronType::ArrayOfMatStructType ArrayOfMatStructType;
	typedef typename InitKronType::GenIjPatchType GenIjPatchType;
	typedef typename InitKronType::GenGroupType GenGroupType;
	typedef typename ArrayOfMatStructType::MatrixDenseOrSparseType MatrixDenseOrSparseType;
	typedef typename PsimagLite::Vector<SizeType>::Type VectorSizeType;

public:

	KronMatrix(const InitKronType& initKron)
	    : initKron_(initKron),
	      vstart_(initKron_.patch(GenIjPatchType::LEFT).size() + 1)
	{
		setUpVstart();
		assert(vstart_.size() > 0);
		SizeType nsize = vstart_[vstart_.size() - 1];
		assert(nsize > 0);
		yin_.resize(nsize, 0.0);
		xout_.resize(nsize, 0.0);

		std::cout<<"KronMatrix: preparation done for size="<<initKron.size()<<"\n";
	}

	void matrixVectorProduct(VectorType& vout, const VectorType& vin) const
	{
		copyIn(xout_, yin_, vout, vin);

		KronConnectionsType kc(initKron_,xout_,yin_,PsimagLite::Concurrency::npthreads);

		typedef PsimagLite::Parallelizer<KronConnectionsType> ParallelizerType;
		ParallelizerType parallelConnections(PsimagLite::Concurrency::npthreads,
		                                     PsimagLite::MPI::COMM_WORLD);

		parallelConnections.loopCreate(kc);
		kc.sync();

		copyOut(vout, xout_);
	}

private:

	// -------------------
	// copy vin(:) to yin(:)
	// -------------------
	void copyIn(VectorType& xout,
	            VectorType& yin,
	            const VectorType& vout,
	            const VectorType& vin) const
	{
		const VectorSizeType& permInverse = initKron_.lrs().super().permutationInverse();
		const SparseMatrixType& left = initKron_.lrs().left().hamiltonian();
		SizeType nl = left.row();

		const SparseMatrixType& right = initKron_.lrs().right().hamiltonian();
		SizeType nr = right.row();

		SizeType nq = initKron_.size();
		SizeType offset = initKron_.offset();
		SizeType npatches = initKron_.patch(GenIjPatchType::LEFT).size();

		for (SizeType ipatch=0; ipatch < npatches; ++ipatch) {

			SizeType igroup = initKron_.patch(GenIjPatchType::LEFT)[ipatch];
			SizeType jgroup = initKron_.patch(GenIjPatchType::RIGHT)[ipatch];

			SizeType sizeLeft =  initKron_.istartLeft()(igroup+1) -
			        initKron_.istartLeft()(igroup);
			SizeType sizeRight = initKron_.istartRight()(jgroup+1) -
			        initKron_.istartRight()(jgroup);

			SizeType left_offset = initKron_.istartLeft()(igroup);
			SizeType right_offset = initKron_.istartRight()(jgroup);

			for (SizeType ileft=0; ileft < sizeLeft; ++ileft) {
				for (SizeType iright=0; iright < sizeRight; ++iright) {

					SizeType i = ileft + left_offset;
					SizeType j = iright + right_offset;

					SizeType ij = i + j * nl;

					assert( (0 <= i) && (i < nl) );
					assert( (0 <= j) && (j < nr) );

					assert( (0 <= ij) && (ij < permInverse.size() ));

					SizeType r = permInverse[ ij ];
					assert( !(  (r < offset) || (r >= (offset + nq)) ) );


					SizeType ip = vstart_[ipatch] + (iright + ileft * sizeRight);
					// SizeType ip = vstart[ipatch] + (ileft + iright * sizeLeft);

					assert( (0 <= ip) && (ip < yin.size()) );

					assert( (0 <= (r-offset)) && ((r-offset) < vin.size()) );
					yin[ip] = vin[r-offset];
					xout[ip] = vout[r-offset];
				}
			}
		}
	}

	// -------------------
	// copy xout(:) to vout(:)
	// -------------------
	void copyOut(VectorType& vout, const VectorType& xout) const
	{
		const VectorSizeType& permInverse = initKron_.lrs().super().permutationInverse();
		const SparseMatrixType& left = initKron_.lrs().left().hamiltonian();
		SizeType nl = left.row();

		const SparseMatrixType& right = initKron_.lrs().right().hamiltonian();
		SizeType nr = right.row();

		SizeType nq = initKron_.size();
		SizeType offset = initKron_.offset();
		SizeType npatches = initKron_.patch(GenIjPatchType::LEFT).size();

		for( SizeType ipatch=0; ipatch < npatches; ++ipatch) {

			SizeType igroup = initKron_.patch(GenIjPatchType::LEFT)[ipatch];
			SizeType jgroup = initKron_.patch(GenIjPatchType::RIGHT)[ipatch];

			SizeType sizeLeft =  initKron_.istartLeft()(igroup+1) -
			        initKron_.istartLeft()(igroup);
			SizeType sizeRight = initKron_.istartRight()(jgroup+1) -
			        initKron_.istartRight()(jgroup);

			SizeType left_offset = initKron_.istartLeft()(igroup);
			SizeType right_offset = initKron_.istartRight()(jgroup);

			for (SizeType ileft=0; ileft < sizeLeft; ++ileft) {
				for (SizeType iright=0; iright < sizeRight; ++iright) {

					SizeType i = ileft + left_offset;
					SizeType j = iright + right_offset;
					SizeType ij = i + j*nl;

					assert( (0 <= i) && (i < nl) );
					assert( (0 <= j) && (j < nr) );

					assert( (0 <= ij) && (ij < permInverse.size()) );

					SizeType r = permInverse[ i + j * nl ];
					assert( !(  (r < offset) || (r >= (offset + nq)) ) );

					SizeType ip = vstart_[ipatch] + (iright + ileft * sizeRight);
					assert( (0 <= ip) && (ip < xout.size()) );

					assert( (0 <= (r-offset)) && ((r-offset) < vout.size()) );
					vout[r-offset] = xout[ip];
				}
			}
		}
	}

	// -------------------------------------------
	// setup vstart(:) for beginning of each patch
	// -------------------------------------------
	void setUpVstart()
	{
		SizeType npatches = initKron_.patch(GenIjPatchType::LEFT).size();

		SizeType ip = 0;

		for (SizeType ipatch=0; ipatch < npatches; ++ipatch) {
			vstart_[ipatch] = ip;

			SizeType igroup = initKron_.patch(GenIjPatchType::LEFT)[ipatch];
			SizeType jgroup = initKron_.patch(GenIjPatchType::RIGHT)[ipatch];

			assert(initKron_.istartLeft()(igroup+1) >= initKron_.istartLeft()(igroup));
			SizeType sizeLeft =  initKron_.istartLeft()(igroup+1) -
			        initKron_.istartLeft()(igroup);

			assert(initKron_.istartRight()(jgroup+1) >= initKron_.istartRight()(jgroup));
			SizeType sizeRight = initKron_.istartRight()(jgroup+1) -
			        initKron_.istartRight()(jgroup);

			assert(1 <= sizeLeft);
			assert(1 <= sizeRight);

			ip += sizeLeft * sizeRight;
		}

		vstart_[npatches] = ip;
	}

	KronMatrix(const KronMatrix&);

	const KronMatrix& operator=(const KronMatrix&);

	const InitKronType& initKron_;

	VectorSizeType vstart_;
	mutable VectorType yin_;
	mutable VectorType xout_;
}; //class KronMatrix

} // namespace PsimagLite

/*@}*/

#endif // KRON_MATRIX_HEADER_H
