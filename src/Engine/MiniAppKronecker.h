#ifndef MINIAPPKRONECKER_H
#define MINIAPPKRONECKER_H
#include "Vector.h"
#include "IoSimple.h"
#include "VerySparseMatrix.h"
#include "Matrix.h"

namespace Dmrg {

template <typename ComplexOrRealType>
class MiniAppKronecker {

	typedef PsimagLite::Vector<SizeType>::Type VectorSizeType;
	typedef VerySparseMatrix<ComplexOrRealType> SparseMatrixType;
	typedef PsimagLite::Matrix<ComplexOrRealType> MatrixType;

public:

	MiniAppKronecker(PsimagLite::String filename)
	{
		SparseMatrixType hamLeft(static_cast<SizeType>(0));
		SparseMatrixType hamRight(static_cast<SizeType>(0));
		PsimagLite::IoSimple::In io(filename);
		VectorSizeType pse;
		io.read(pse,"#SuperBasisPermutation");
		std::cerr<<"Read pse_.size="<<pse.size()<<"\n";
		pse_ = pse;
		for (SizeType i = 0; i < pse_.size(); ++i)
			pse_[pse[i]] = i;

		io.readMatrix(hamLeft,"#LeftHamiltonian");
		std::cerr<<"Read H_L square, rank="<<hamLeft.rank()<<"\n";
		assert(isHermitian(hamLeft));
		io.rewind();
		io.advance("#LeftHamiltonian");

		io.readMatrix(hamRight,"#RightHamiltonian");
		std::cerr<<"Read H_R square, rank="<<hamRight.rank()<<"\n";
		assert(isHermitian(hamRight));
		io.rewind();
		io.advance("#RightHamiltonian");

		buildHLeftAndRight(hamLeft,hamRight);

//		SparseMatrixType Ahat(static_cast<SizeType>(0));
//		SparseMatrixType B(static_cast<SizeType>(0));
//		while (!io.eof()) {
//			io.readMatrix(Ahat,"#Ahat");
//			io.rewind();
//			io.advance("#Ahat");
//			io.readMatrix(B,"#B");
//			io.rewind();
//			io.advance("#B");
//			buildHconnection(Ahat,B);
//		}
	}

	void printH(std::ostream& os) const
	{
		SizeType n = hamSuper_.n_row();
		os<<"P1\n";
		os<<n<<" "<<n<<"\n";
		for (SizeType i = 0; i < n; ++i) {
			for (SizeType j = 0; j < n; ++j) {
				if (fabs(hamSuper_(i,j)) < 1e-6)
					os<<"0 ";
				else
					os<<"1 ";
			}

			os<<"\n";
		}
	}

private:

	void buildHconnection(const SparseMatrixType Ahat,
	                      const SparseMatrixType B)
	{

	}

	void buildHLeftAndRight(const SparseMatrixType hamLeft,
	                        const SparseMatrixType hamRight)
	{
		SizeType nLeft = hamLeft.rank();
		SizeType nRight = hamRight.rank();
		SizeType nSuper = nLeft*nRight;
		hamSuper_.resize(nSuper,nSuper);
		hamSuper_.setTo(0.0);

		// hamLeft
		for (SizeType i = 0; i < nLeft; ++i) {
			for (SizeType j = 0; j < nRight; ++j) { // j = jprime
				SizeType is = pack(i,j,nLeft);
				for (SizeType ip = 0; ip < nLeft; ++ip) { // iprime
					SizeType js = pack(ip,j,nLeft);
					hamSuper_(is,js) += hamLeft(i,ip);
				}
			}
		}

		std::cerr<<"Done hamLeft\n";

		// hamRight
		for (SizeType i = 0; i < nRight; ++i) {
			for (SizeType j = 0; j < nLeft; ++j) { // j = jprime
				SizeType is =  pack(j,i,nLeft);
				for (SizeType ip = 0; ip < nRight; ++ip) { // iprime
					SizeType js = pack(j,ip,nLeft);
					hamSuper_(is,js) += hamRight(i,ip);
				}
			}
		}

		std::cerr<<"Done hamRight\n";
	}

	SizeType pack(SizeType l, SizeType r, SizeType nLeft) const
	{
		assert(l + r*nLeft < pse_.size());
		return pse_[l + r*nLeft];
	}

	VectorSizeType pse_;
	MatrixType hamSuper_;
}; // class MiniAppKronecker
}

#endif // MINIAPPKRONECKER_H
