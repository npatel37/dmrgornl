/*
Copyright (c) 2009-2017, UT-Battelle, LLC
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

#ifndef DENSITY_MATRIX_SVD_H
#define DENSITY_MATRIX_SVD_H
#include "ProgressIndicator.h"
#include "TypeToString.h"
#include "DensityMatrixBase.h"
#include "NoPthreads.h"
#include "Concurrency.h"
#include "MatrixVectorKron/GenIjPatch.h"

namespace Dmrg {

template<typename TargettingType>
class DensityMatrixSvd : public DensityMatrixBase<TargettingType> {

	typedef DensityMatrixBase<TargettingType> BaseType;
	typedef typename TargettingType::BasisWithOperatorsType BasisWithOperatorsType;
	typedef typename TargettingType::LeftRightSuperType LeftRightSuperType;
	typedef typename BasisWithOperatorsType::BasisType BasisType;
	typedef typename BasisWithOperatorsType::SparseMatrixType SparseMatrixType;
	typedef typename SparseMatrixType::value_type ComplexOrRealType;
	typedef typename TargettingType::VectorWithOffsetType VectorWithOffsetType;
	typedef typename BaseType::BuildingBlockType MatrixType;
	typedef PsimagLite::Matrix<SizeType> MatrixSizeType;
	typedef typename PsimagLite::Vector<MatrixType*>::Type VectorMatrixType;
	typedef PsimagLite::Concurrency ConcurrencyType;
	typedef typename BasisType::FactorsType FactorsType;
	typedef PsimagLite::ProgressIndicator ProgressIndicatorType;
	typedef typename PsimagLite::Real<ComplexOrRealType>::Type RealType;
	typedef typename BaseType::Params ParamsType;
	typedef GenIjPatch<LeftRightSuperType> GenIjPatchType;
	typedef typename GenIjPatchType::VectorSizeType VectorSizeType;
	typedef typename PsimagLite::Vector<RealType>::Type VectorRealType;
	typedef typename PsimagLite::Vector<GenIjPatchType*>::Type VectorGenIjPatchType;
	typedef std::pair<SizeType, SizeType> PairSizeType;
	typedef typename BaseType::BlockDiagonalMatrixType BlockDiagonalMatrixType;

	enum {EXPAND_SYSTEM = ProgramGlobals::EXPAND_SYSTEM };

	class GroupsStruct {

		struct PropsOfGroup {
			PropsOfGroup(SizeType t, SizeType s, SizeType j)
			    : target(t), sector(s), jgroup(j)
			{}

			SizeType target;
			SizeType sector;
			SizeType jgroup;
			SizeType offset;
		};

		typedef typename PsimagLite::Vector<PropsOfGroup>::Type VectorPropsOfGroupType;

	public:

		GroupsStruct(const LeftRightSuperType& lrs,
		             ProgramGlobals::DirectionEnum direction)
		    : lrs_(lrs),
		      direction_(direction),
		      propsThisIgroup_(this->basis().partition())
		{}

		~GroupsStruct()
		{
			SizeType n = m_.size();
			for (SizeType i = 0; i < n; ++i) {
				delete m_[i];
				m_[i] = 0;
			}
		}

		void push(SizeType igroup,
		          SizeType jgroup,
		          SizeType target,
		          SizeType sector)
		{
			// have I seen this group before?
			typename VectorSizeType::iterator it = std::find(seenGroups_.begin(),
			                                                 seenGroups_.end(),
			                                                 igroup);
			if (it == seenGroups_.end()) { // No --> create group
				seenGroups_.push_back(igroup);
				// included repeted jgroups here
				assert(igroup < propsThisIgroup_.size());
				propsThisIgroup_[igroup].push_back(PropsOfGroup(target, sector, jgroup));

			} else { //  Yes, add to group
				assert(igroup < propsThisIgroup_.size());
				propsThisIgroup_[igroup].push_back(PropsOfGroup(target, sector, jgroup));
			}
		}

		void finalize()
		{
			SizeType n = seenGroups_.size();
			m_.resize(n, 0);
			for (SizeType i = 0; i < n; ++i) {
				SizeType igroup = seenGroups_[i];
				SizeType offset = this->basis().partition(igroup);
				SizeType rows = this->basis().partition(igroup + 1) - offset;
				SizeType cols = 0;
				SizeType m = propsThisIgroup_[igroup].size();
				for (SizeType j = 0; j < m; ++j) {
					SizeType jgroup = propsThisIgroup_[igroup][j].jgroup;
					SizeType joffset = this->basisPrime().partition(jgroup);
					SizeType jsize = this->basisPrime().partition(jgroup + 1) - joffset;
					propsThisIgroup_[igroup][j].offset = cols;
					cols += jsize;
				}

				m_[i] = new MatrixType(rows, cols);
				m_[i]->setTo(0.0);
			}
		}

		// TODO: Move matrix out
		MatrixType& matrix(SizeType igroup)
		{
			SizeType index = groupIndex(igroup);
			assert(index < m_.size());
			assert(m_[index]);
			return *(m_[index]);
		}

		const BasisWithOperatorsType& basis() const
		{
			return (expandSys()) ? lrs_.left() : lrs_.right();
		}

		const BasisWithOperatorsType& basisPrime() const
		{
			return (expandSys()) ? lrs_.right() : lrs_.left();
		}

		SizeType groupFromIndex(SizeType index) const
		{
			assert(index < seenGroups_.size());
			return seenGroups_[index];
		}

		SizeType groupPrimeIndex(SizeType target, SizeType sector, SizeType igroup) const
		{
			SizeType m = propsThisIgroup_[igroup].size();
			for (SizeType j = 0; j < m; ++j) {
				if (propsThisIgroup_[igroup][j].target == target &&
				        propsThisIgroup_[igroup][j].sector == sector)
					return propsThisIgroup_[igroup][j].jgroup;
			}

			throw PsimagLite::RuntimeError("GroupsStruct: groupPrimeIndex\n");
		}

		SizeType additionalOffset(SizeType igroup,
		                          SizeType target,
		                          SizeType sector,
		                          SizeType jgroup) const
		{
			SizeType m = propsThisIgroup_[igroup].size();
			for (SizeType j = 0; j < m; ++j) {
				if (propsThisIgroup_[igroup][j].target == target &&
				        propsThisIgroup_[igroup][j].sector == sector &&
				        propsThisIgroup_[igroup][j].jgroup == jgroup)
					return propsThisIgroup_[igroup][j].offset;
			}

			throw PsimagLite::RuntimeError("GroupsStruct: additionalOffset\n");
		}

		SizeType size() const
		{
			return seenGroups_.size();
		}

		bool expandSys() const
		{
			return (direction_ == ProgramGlobals::EXPAND_SYSTEM);
		}

	private:

		SizeType groupIndex(SizeType igroup) const
		{
			typename VectorSizeType::const_iterator it = std::find(seenGroups_.begin(),
			                                                       seenGroups_.end(),
			                                                       igroup);
			assert(it != seenGroups_.end());
			return it - seenGroups_.begin();
		}


		const LeftRightSuperType& lrs_;
		ProgramGlobals::DirectionEnum direction_;
		VectorSizeType seenGroups_;
		typename PsimagLite::Vector<VectorPropsOfGroupType>::Type propsThisIgroup_;
		// TODO: Move matrix out
		VectorMatrixType m_;
	};

	typedef GroupsStruct GroupsStructType;

	class ParallelPsiSplit {

	public:

		ParallelPsiSplit(const LeftRightSuperType& lrs,
		                 const GenIjPatchType& ijPatch,
		                 const VectorWithOffsetType& v,
		                 SizeType target,
		                 SizeType sector,
		                 RealType sqrtW,
		                 GroupsStructType& allTargets)
		    : lrs_(lrs),
		      ijPatch_(ijPatch),
		      v_(v),
		      target_(target),
		      sector_(sector),
		      sqrtW_(sqrtW),
		      allTargets_(allTargets)
		{}

		void doTask(SizeType ipatch, SizeType)
		{
			SizeType igroup = ijPatch_(GenIjPatchType::LEFT)[ipatch];
			SizeType jgroup = ijPatch_(GenIjPatchType::RIGHT)[ipatch];
			bool expandSys = allTargets_.expandSys();
			SizeType groupBig = (expandSys) ? igroup : jgroup;
			MatrixType& matrix = allTargets_.matrix(groupBig);
			SizeType m = v_.sector(sector_);
			SizeType offset = v_.offset(m);
			SizeType nl = lrs_.left().size();
			SizeType rowOffset = allTargets_.basis().partition(groupBig);
			SizeType rows = allTargets_.basis().partition(groupBig + 1) - rowOffset;
			SizeType groupSmall = allTargets_.groupPrimeIndex(target_, sector_, groupBig);
			SizeType colOffset = allTargets_.basisPrime().partition(groupSmall);
			SizeType cols = allTargets_.basisPrime().partition(groupSmall + 1) - colOffset;
			SizeType additionalOffset = allTargets_.additionalOffset(groupBig,
			                                                         target_,
			                                                         sector_,
			                                                         groupSmall);

			for (SizeType ind = 0; ind < rows; ++ind) {
				for (SizeType jnd = 0; jnd < cols; ++jnd) {

					SizeType i = ind + rowOffset;
					SizeType j = jnd + colOffset;

					SizeType ij = (expandSys) ? i + j * nl : j + i*nl;

					assert(!expandSys || (i < nl && j < lrs_.right().size()));
					assert(expandSys || (j < nl && i < lrs_.right().size()));

					assert(ij < lrs_.super().permutationInverse().size());

					SizeType r = lrs_.super().permutationInverse()[ij];
					if (r < offset || r >= offset + v_.effectiveSize(m))
						continue;

					matrix(ind, jnd + additionalOffset) +=  sqrtW_*v_.slowAccess(r);
				}
			}
		}

		SizeType tasks() const
		{
			return ijPatch_(GenIjPatchType::LEFT).size();
		}

	private:

		const LeftRightSuperType& lrs_;
		const GenIjPatchType& ijPatch_;
		const VectorWithOffsetType& v_;
		SizeType target_;
		SizeType sector_;
		RealType sqrtW_;
		GroupsStructType& allTargets_;
	};

	class ParallelSvd {

	public:

		ParallelSvd(BlockDiagonalMatrixType& blockDiagonalMatrix,
		            GroupsStructType& allTargets,
		            VectorRealType& eigs)
		    :  blockDiagonalMatrix_(blockDiagonalMatrix),
		      allTargets_(allTargets),
		      eigs_(eigs)
		{
			SizeType oneSide = allTargets.basis().size();
			eigs_.resize(oneSide);
			std::fill(eigs_.begin(), eigs_.end(), 0.0);
		}

		void doTask(SizeType ipatch, SizeType)
		{
			SizeType igroup = allTargets_.groupFromIndex(ipatch);
			MatrixType& m = allTargets_.matrix(igroup);
			MatrixType vt;
			VectorRealType eigsOnePatch;

			svd('A', m, eigsOnePatch, vt);

			const BasisType& basis = allTargets_.basis();
			SizeType offset = basis.partition(igroup);
			SizeType partSize = basis.partition(igroup + 1) - offset;
			assert(m.rows() == partSize);
			assert(m.rows() == m.cols());
			blockDiagonalMatrix_.setBlock(igroup, offset, m);
			SizeType x = eigsOnePatch.size();
			if (x > partSize) x = partSize;
			assert(x + offset <= eigs_.size());
			for (SizeType i = 0; i < x; ++i)
				eigs_[i + offset] = eigsOnePatch[i]*eigsOnePatch[i];
		}

		SizeType tasks() const
		{
			return allTargets_.size();
		}

	private:

		BlockDiagonalMatrixType& blockDiagonalMatrix_;
		GroupsStructType& allTargets_;
		VectorRealType& eigs_;
	};

public:

	DensityMatrixSvd(const TargettingType& target,
	                 const LeftRightSuperType& lrs,
	                 const ParamsType& p)
	    :
	      progress_("DensityMatrixSvd"),
	      lrs_(lrs),
	      params_(p),
	      allTargets_(lrs, p.direction),
	      data_(allTargets_.basis())
	{
		SizeType oneOrZero = (target.includeGroundStage()) ? 1 : 0;
		SizeType targets = oneOrZero + target.size(); // Number of targets;
		typename GenIjPatchType::LeftOrRightEnumType dir1 =
		        (p.direction == ProgramGlobals::EXPAND_SYSTEM) ?
		            GenIjPatchType::LEFT : GenIjPatchType::RIGHT;
		typename GenIjPatchType::LeftOrRightEnumType dir2 =
		        (p.direction == ProgramGlobals::EXPAND_SYSTEM) ?
		            GenIjPatchType::RIGHT : GenIjPatchType::LEFT;

		for (SizeType x = 0; x  < targets; ++x) {
			SizeType x2 = (target.includeGroundStage() && x > 0 ) ? x - 1 : x;

			const VectorWithOffsetType& v = (target.includeGroundStage() && x == 0) ?
			            target.gs() : target(x2);

			SizeType sectors = v.sectors();
			for (SizeType sector = 0; sector < sectors; ++sector) {
				SizeType m = v.sector(sector);
				int state = lrs_.super().partition(m);
				SizeType qn = lrs_.super().qn(state);
				GenIjPatchType genIjPatch(lrs, qn);
				const VectorSizeType& groups =  genIjPatch(dir1);
				for (SizeType i = 0; i < groups.size(); ++i) {
					SizeType igroup = groups[i];
					assert(genIjPatch(dir2).size() > i);
					SizeType jgroup = genIjPatch(dir2)[i];
					allTargets_.push(igroup, jgroup, x, sector);
				}
			}
		}

		allTargets_.finalize();

		{
			PsimagLite::OstringStream msg;
			msg<<"Found "<<allTargets_.size()<<" groups on left or right";
			progress_.printline(msg,std::cout);
		}

		for (SizeType x = 0; x < targets; ++x)
			addThisTarget(x, target);

		{
			PsimagLite::OstringStream msg;
			msg<<"Done with init partition";
			progress_.printline(msg,std::cout);
		}
	}

	virtual const BlockDiagonalMatrixType& operator()()
	{
		return data_;
	}

	void diag(VectorRealType& eigs,char jobz)
	{
		typedef PsimagLite::Parallelizer<ParallelSvd> ParallelizerType;
		ParallelizerType threaded(PsimagLite::Concurrency::npthreads,
		                          PsimagLite::MPI::COMM_WORLD);
		ParallelSvd parallelSvd(data_,
		                        allTargets_,
		                        eigs);
		threaded.loopCreate(parallelSvd);
		for (SizeType i = 0; i < data_.blocks(); ++i) {
			SizeType n = data_(i).rows();
			if (n > 0) continue;
			SizeType offset = allTargets_.basis().partition(i);
			SizeType part = allTargets_.basis().partition(i + 1) - offset;
			MatrixType m(part, part);
			m.setTo(0.0);
			for (SizeType j = 0; j < part; ++j)
				m(j, j) = 1.0;
			data_.setBlock(i, offset, m);
		}

		data_.enforcePhase();
	}

	friend std::ostream& operator<<(std::ostream& os,
	                                const DensityMatrixSvd& dm)
	{
		for (SizeType m = 0; m < dm.data_.blocks(); ++m) {
			SizeType ne = dm.pBasis_.electrons(dm.pBasis_.partition(m));
			os<<" ne="<<ne<<"\n";
			os<<dm.data_(m)<<"\n";
		}

		return os;
	}

private:

	void addThisTarget(SizeType x,
	                   const TargettingType& target)

	{
		SizeType x2 = (target.includeGroundStage() && x > 0 ) ? x - 1 : x;

		const VectorWithOffsetType& v = (target.includeGroundStage() && x == 0) ?
		            target.gs() : target(x2);

		RealType weight = (target.includeGroundStage() && x == 0 ) ?
		            target.gsWeight() : target.weight(x2);

		addThisTarget2(x, v, sqrt(weight));
	}

	void addThisTarget2(SizeType x,
	                    const VectorWithOffsetType& v,
	                    RealType sqrtW)
	{
		const BasisType& super = lrs_.super();
		for (SizeType sector = 0; sector < v.sectors(); ++sector) {
			SizeType m = v.sector(sector);
			int state = super.partition(m);
			SizeType qn = super.qn(state);
			GenIjPatchType genIjPatch(lrs_, qn);
			typedef PsimagLite::Parallelizer<ParallelPsiSplit> ParallelizerType;
			ParallelizerType threaded(PsimagLite::Concurrency::npthreads,
			                          PsimagLite::MPI::COMM_WORLD);
			ParallelPsiSplit parallelPsiSplit(lrs_,
			                                  genIjPatch,
			                                  v,
			                                  x,
			                                  sector,
			                                  sqrtW,
			                                  allTargets_);
			threaded.loopCreate(parallelPsiSplit);
		}
	}

	ProgressIndicatorType progress_;
	const LeftRightSuperType& lrs_;
	const ParamsType& params_;
	GroupsStructType allTargets_;
	BlockDiagonalMatrixType data_;
}; // class DensityMatrixSvd

} // namespace Dmrg

#endif

