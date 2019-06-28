//------------------------------------------------------------------------------
// Copyright (c) 2017, University of Tennessee
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the University of Tennessee nor the
//       names of its contributors may be used to endorse or promote products
//       derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL UNIVERSITY OF TENNESSEE BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//------------------------------------------------------------------------------
// This research was supported by the Exascale Computing Project (17-SC-20-SC),
// a collaborative effort of two U.S. Department of Energy organizations (Office
// of Science and the National Nuclear Security Administration) responsible for
// the planning and preparation of a capable exascale ecosystem, including
// software, applications, hardware, advanced system engineering and early
// testbed platforms, in support of the nation's exascale computing imperative.
//------------------------------------------------------------------------------
// For assistance with SLATE, email <slate-user@icl.utk.edu>.
// You can also join the "SLATE User" Google group by going to
// https://groups.google.com/a/icl.utk.edu/forum/#!forum/slate-user,
// signing in with your Google credentials, and then clicking "Join group".
//------------------------------------------------------------------------------

#ifndef SLATE_HERMITIAN_MATRIX_HH
#define SLATE_HERMITIAN_MATRIX_HH

#include "slate/BaseTrapezoidMatrix.hh"
#include "slate/Matrix.hh"
#include "slate/Tile.hh"
#include "slate/types.hh"

#include "lapack.hh"

#include <algorithm>
#include <utility>

#include "slate/internal/mpi.hh"

namespace slate {

//==============================================================================
/// Hermitian, n-by-n, distributed, tiled matrices.
template <typename scalar_t>
class HermitianMatrix: public BaseTrapezoidMatrix<scalar_t> {
public:
    using ij_tuple = typename BaseMatrix<scalar_t>::ij_tuple;

    // constructors
    HermitianMatrix();

    HermitianMatrix(Uplo uplo, int64_t n,
                    std::function<int64_t (int64_t j)>& inTileNb,
                    std::function<int (ij_tuple ij)>& inTileRank,
                    std::function<int (ij_tuple ij)>& inTileDevice,
                    MPI_Comm mpi_comm);

    HermitianMatrix(Uplo uplo, int64_t n, int64_t nb,
                    int p, int q, MPI_Comm mpi_comm);

    static
    HermitianMatrix fromLAPACK(Uplo uplo, int64_t n,
                               scalar_t* A, int64_t lda, int64_t nb,
                               int p, int q, MPI_Comm mpi_comm);

    static
    HermitianMatrix fromScaLAPACK(Uplo uplo, int64_t n,
                                  scalar_t* A, int64_t lda, int64_t nb,
                                  int p, int q, MPI_Comm mpi_comm);

    static
    HermitianMatrix fromDevices(Uplo uplo, int64_t n,
                                scalar_t** Aarray, int num_devices, int64_t lda,
                                int64_t nb, int p, int q, MPI_Comm mpi_comm);

    // conversion
    explicit HermitianMatrix(BaseTrapezoidMatrix<scalar_t>& orig);

    HermitianMatrix(Uplo uplo, BaseMatrix<scalar_t>& orig);

    // on-diagonal sub-matrix
    HermitianMatrix sub(int64_t i1, int64_t i2);

    // off-diagonal sub-matrix
    Matrix<scalar_t> sub(int64_t i1, int64_t i2, int64_t j1, int64_t j2);

    template <typename out_scalar_t=scalar_t>
    HermitianMatrix<out_scalar_t> emptyLike(int64_t nb=0,
                                            Op deepOp=Op::NoTrans);

protected:
    // used by fromLAPACK and fromScaLAPACK
    HermitianMatrix(Uplo uplo, int64_t n,
                    scalar_t* A, int64_t lda, int64_t nb,
                    int p, int q, MPI_Comm mpi_comm, bool is_scalapack);

    // used by fromDevices
    HermitianMatrix(Uplo uplo, int64_t n,
                    scalar_t** Aarray, int num_devices, int64_t lda,
                    int64_t nb, int p, int q, MPI_Comm mpi_comm);

    // used by on-diagonal sub(i1, i2)
    HermitianMatrix(HermitianMatrix& orig,
                    int64_t i1, int64_t i2);

public:
    template <typename T>
    friend void swap(HermitianMatrix<T>& A, HermitianMatrix<T>& B);
};

//------------------------------------------------------------------------------
/// Default constructor creates an empty matrix.
template <typename scalar_t>
HermitianMatrix<scalar_t>::HermitianMatrix()
    : BaseTrapezoidMatrix<scalar_t>()
{}

//------------------------------------------------------------------------------
/// Constructor creates an n-by-n matrix, with no tiles allocated,
/// where tileNb, tileRank, tileDevice are given as functions.
/// Tiles can be added with tileInsert().
///
template <typename scalar_t>
HermitianMatrix<scalar_t>::HermitianMatrix(
    Uplo uplo, int64_t n,
    std::function<int64_t (int64_t j)>& inTileNb,
    std::function<int (ij_tuple ij)>& inTileRank,
    std::function<int (ij_tuple ij)>& inTileDevice,
    MPI_Comm mpi_comm)
    : BaseTrapezoidMatrix<scalar_t>(uplo, n, n, inTileNb, inTileRank,
                                    inTileDevice, mpi_comm)
{}

//------------------------------------------------------------------------------
/// Constructor creates an n-by-n matrix, with no tiles allocated,
/// with fixed nb-by-nb tile size and 2D block cyclic distribution.
/// Tiles can be added with tileInsert().
///
template <typename scalar_t>
HermitianMatrix<scalar_t>::HermitianMatrix(
    Uplo uplo, int64_t n, int64_t nb, int p, int q, MPI_Comm mpi_comm)
    : BaseTrapezoidMatrix<scalar_t>(uplo, n, n, nb, p, q, mpi_comm)
{}

//------------------------------------------------------------------------------
/// [static]
/// Named constructor returns a new Matrix from LAPACK layout.
/// Construct matrix by wrapping existing memory of an n-by-n lower
/// or upper Hermitian LAPACK-style matrix.
///
/// @param[in] uplo
///     - Upper: upper triangle of A is stored.
///     - Lower: lower triangle of A is stored.
///
/// @param[in] n
///     Number of rows and columns of the matrix. n >= 0.
///
/// @param[in,out] A
///     The n-by-n Hermitian matrix A, stored in an lda-by-n array.
///
/// @param[in] lda
///     Leading dimension of the array A. lda >= m.
///
/// @param[in] nb
///     Block size in 2D block-cyclic distribution. nb > 0.
///
/// @param[in] p
///     Number of block rows in 2D block-cyclic distribution. p > 0.
///
/// @param[in] q
///     Number of block columns of 2D block-cyclic distribution. q > 0.
///
/// @param[in] mpi_comm
///     MPI communicator to distribute matrix across.
///     p*q == MPI_Comm_size(mpi_comm).
///
template <typename scalar_t>
HermitianMatrix<scalar_t> HermitianMatrix<scalar_t>::fromLAPACK(
    Uplo uplo, int64_t n,
    scalar_t* A, int64_t lda, int64_t nb,
    int p, int q, MPI_Comm mpi_comm)
{
    return HermitianMatrix<scalar_t>(uplo, n, A, lda, nb, p, q, mpi_comm, false);
}

//------------------------------------------------------------------------------
/// [static]
/// Named constructor returns a new Matrix from ScaLAPACK layout.
/// Construct matrix by wrapping existing memory of an n-by-n lower
/// or upper Hermitian ScaLAPACK-style matrix.
/// @see BaseTrapezoidMatrix
///
/// @param[in] uplo
///     - Upper: upper triangle of A is stored.
///     - Lower: lower triangle of A is stored.
///
/// @param[in] n
///     Number of rows and columns of the matrix. n >= 0.
///
/// @param[in,out] A
///     The local portion of the 2D block cyclic distribution of
///     the n-by-n matrix A, with local leading dimension lda.
///
/// @param[in] lda
///     Local leading dimension of the array A. lda >= local number of rows.
///
/// @param[in] nb
///     Block size in 2D block-cyclic distribution. nb > 0.
///
/// @param[in] p
///     Number of block rows in 2D block-cyclic distribution. p > 0.
///
/// @param[in] q
///     Number of block columns of 2D block-cyclic distribution. q > 0.
///
/// @param[in] mpi_comm
///     MPI communicator to distribute matrix across.
///     p*q == MPI_Comm_size(mpi_comm).
///
template <typename scalar_t>
HermitianMatrix<scalar_t> HermitianMatrix<scalar_t>::fromScaLAPACK(
    Uplo uplo, int64_t n,
    scalar_t* A, int64_t lda, int64_t nb,
    int p, int q, MPI_Comm mpi_comm)
{
    return HermitianMatrix<scalar_t>(uplo, n, A, lda, nb, p, q, mpi_comm, true);
}

//------------------------------------------------------------------------------
/// [static]
/// TODO
/// Named constructor returns a new Matrix from ScaLAPACK layout.
/// Construct matrix by wrapping existing memory of an n-by-n lower
/// or upper Hermitian ScaLAPACK-style matrix.
/// @see BaseTrapezoidMatrix
///
/// @param[in] uplo
///     - Upper: upper triangle of A is stored.
///     - Lower: lower triangle of A is stored.
///
/// @param[in] n
///     Number of rows and columns of the matrix. n >= 0.
///
/// @param[in,out] Aarray
///     TODO
///     The local portion of the 2D block cyclic distribution of
///     the n-by-n matrix A, with local leading dimension lda.
///
/// @param[in] num_devices
///     TODO
///
/// @param[in] lda
///     Local leading dimension of the array A. lda >= local number of rows.
///
/// @param[in] nb
///     Block size in 2D block-cyclic distribution. nb > 0.
///
/// @param[in] p
///     Number of block rows in 2D block-cyclic distribution. p > 0.
///
/// @param[in] q
///     Number of block columns of 2D block-cyclic distribution. q > 0.
///
/// @param[in] mpi_comm
///     MPI communicator to distribute matrix across.
///     p*q == MPI_Comm_size(mpi_comm).
///
template <typename scalar_t>
HermitianMatrix<scalar_t> HermitianMatrix<scalar_t>::fromDevices(
    Uplo uplo, int64_t n,
    scalar_t** Aarray, int num_devices, int64_t lda, int64_t nb,
    int p, int q, MPI_Comm mpi_comm)
{
    return HermitianMatrix<scalar_t>(uplo, n, Aarray, num_devices, lda, nb,
                                     p, q, mpi_comm);
}

//------------------------------------------------------------------------------
/// @see fromLAPACK
/// @see fromScaLAPACK
///
/// @param[in] is_scalapack
///     If true,  A is a ScaLAPACK matrix.
///     If false, A is an LAPACK matrix.
///
template <typename scalar_t>
HermitianMatrix<scalar_t>::HermitianMatrix(
    Uplo uplo, int64_t n,
    scalar_t* A, int64_t lda, int64_t nb,
    int p, int q, MPI_Comm mpi_comm, bool is_scalapack)
    : BaseTrapezoidMatrix<scalar_t>(uplo, n, n, A, lda, nb, p, q, mpi_comm,
                                    is_scalapack)
{}

//------------------------------------------------------------------------------
/// @see fromDevices
///
template <typename scalar_t>
HermitianMatrix<scalar_t>::HermitianMatrix(
    Uplo uplo, int64_t n,
    scalar_t** Aarray, int num_devices, int64_t lda, int64_t nb,
    int p, int q, MPI_Comm mpi_comm)
    : BaseTrapezoidMatrix<scalar_t>(uplo, n, n, Aarray, num_devices, lda, nb,
                                    p, q, mpi_comm)
{}

//------------------------------------------------------------------------------
/// [explicit]
/// Conversion from trapezoid, triangular, symmetric, or Hermitian matrix
/// creates a shallow copy view of the original matrix.
/// Orig must be square -- slice beforehand if needed.
///
/// @param[in,out] orig
///     Original matrix.
///
template <typename scalar_t>
HermitianMatrix<scalar_t>::HermitianMatrix(
    BaseTrapezoidMatrix<scalar_t>& orig)
    : BaseTrapezoidMatrix<scalar_t>(orig)
{
    slate_assert(orig.mt() == orig.nt());
    slate_assert(orig.m() == orig.n());
}

//------------------------------------------------------------------------------
/// Conversion from general matrix
/// creates a shallow copy view of the original matrix.
/// Orig must be square -- slice beforehand if needed.
///
/// @param[in] uplo
///     - Upper: upper triangle of A is stored.
///     - Lower: lower triangle of A is stored.
///
/// @param[in,out] orig
///     Original matrix.
///
template <typename scalar_t>
HermitianMatrix<scalar_t>::HermitianMatrix(
    Uplo uplo, BaseMatrix<scalar_t>& orig)
    : BaseTrapezoidMatrix<scalar_t>(uplo, orig)
{
    slate_assert(orig.mt() == orig.nt());
    slate_assert(orig.m() == orig.n());
}

//------------------------------------------------------------------------------
/// Sub-matrix constructor creates shallow copy view of parent matrix,
/// A[ i1:i2, i1:i2 ]. The new view is still a Hermitian matrix, with the
/// same diagonal as the parent matrix.
///
/// @param[in,out] orig
///     Original matrix.
///
/// @param[in] i1
///     Starting block row and column index. 0 <= i1 < mt.
///
/// @param[in] i2
///     Ending block row and column index (inclusive). i2 < mt.
///
template <typename scalar_t>
HermitianMatrix<scalar_t>::HermitianMatrix(
    HermitianMatrix& orig,
    int64_t i1, int64_t i2)
    : BaseTrapezoidMatrix<scalar_t>(orig, i1, i2, i1, i2)
{}

//------------------------------------------------------------------------------
/// Returns sub-matrix that is a shallow copy view of the
/// parent matrix, A[ i1:i2, i1:i2 ].
/// This version returns a HermitianMatrix with the same diagonal as the
/// parent matrix.
/// @see Matrix TrapezoidMatrix::sub(int64_t i1, int64_t i2,
///                                  int64_t j1, int64_t j2)
///
/// @param[in] i1
///     Starting block row and column index. 0 <= i1 < mt.
///
/// @param[in] i2
///     Ending block row and column index (inclusive). i2 < mt.
///
template <typename scalar_t>
HermitianMatrix<scalar_t> HermitianMatrix<scalar_t>::sub(
    int64_t i1, int64_t i2)
{
    return HermitianMatrix<scalar_t>(*this, i1, i2);
}

//------------------------------------------------------------------------------
/// Returns off-diagonal sub-matrix that is a shallow copy view of the
/// parent matrix, A[ i1:i2, j1:j2 ].
/// This version returns a general Matrix, which:
/// - if uplo = Lower, is strictly below the diagonal, or
/// - if uplo = Upper, is strictly above the diagonal.
/// @see TrapezoidMatrix sub(int64_t i1, int64_t i2)
///
/// @param[in] i1
///     Starting block row index. 0 <= i1 < mt.
///
/// @param[in] i2
///     Ending block row index (inclusive). i2 < mt.
///
/// @param[in] j1
///     Starting block column index. 0 <= j1 < nt.
///
/// @param[in] j2
///     Ending block column index (inclusive). j2 < nt.
///
template <typename scalar_t>
Matrix<scalar_t> HermitianMatrix<scalar_t>::sub(
    int64_t i1, int64_t i2,
    int64_t j1, int64_t j2)
{
    return BaseTrapezoidMatrix<scalar_t>::sub(i1, i2, j1, j2);
}

//------------------------------------------------------------------------------
/// Swaps contents of matrices A and B.
//
// (This isn't really needed over BaseTrapezoidMatrix swap, but is here as a
// reminder in case any members are added that aren't in BaseTrapezoidMatrix.)
template <typename scalar_t>
void swap(HermitianMatrix<scalar_t>& A, HermitianMatrix<scalar_t>& B)
{
    using std::swap;
    swap(static_cast< BaseTrapezoidMatrix<scalar_t>& >(A),
         static_cast< BaseTrapezoidMatrix<scalar_t>& >(B));
}

//------------------------------------------------------------------------------
/// Named constructor returns a new, empty Matrix with the same structure
/// (size and distribution) as this matrix. Tiles are not allocated.
///
template <typename scalar_t>
template <typename out_scalar_t>
HermitianMatrix<out_scalar_t> HermitianMatrix<scalar_t>::emptyLike(
    int64_t nb, Op deepOp)
{
    auto B = this->template baseEmptyLike<out_scalar_t>(nb, nb, deepOp);
    return HermitianMatrix<out_scalar_t>(this->uplo(), B);
}

} // namespace slate

#endif // SLATE_HERMITIAN_MATRIX_HH
