// Copyright (c) 2017-2020, University of Tennessee. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// This program is free software: you can redistribute it and/or modify it under
// the terms of the BSD 3-Clause license. See the accompanying LICENSE file.

#include "slate/slate.hh"
#include "test.hh"

#include "scalapack_wrappers.hh"
#include "scalapack_support_routines.hh"
#include "scalapack_copy.hh"
#include "print_matrix.hh"
#include "grid_utils.hh"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#define SLATE_HAVE_SCALAPACK
//------------------------------------------------------------------------------
template<typename scalar_t>
void test_set_work(Params& params, bool run)
{
    using real_t = blas::real_type<scalar_t>;
    using blas::real;
    using blas::imag;
    using slate::ceildiv;

    // Constants
    const scalar_t one = 1.0;

    // get & mark input values
    scalar_t alpha = params.alpha.get<real_t>();
    scalar_t beta = params.beta.get<real_t>();
    slate::Uplo uplo = slate::Uplo::General;
    int64_t m = params.dim.m();
    int64_t n = params.dim.n();
    int64_t nb = params.nb();
    int64_t p = params.grid.m();
    int64_t q = params.grid.n();
    bool ref_only = params.ref() == 'o';
    bool ref = params.ref() == 'y' || ref_only;
    bool check = params.check() == 'y' && ! ref_only;
    bool trace = params.trace() == 'y';
    int verbose = params.verbose();
    slate::Origin origin = params.origin();
    slate::Target target = params.target();
    params.matrix.mark();

    // mark non-standard output values
    params.time();
    params.ref_time();

    if (! run)
        return;

    slate::Options const opts =  {
        {slate::Option::Target, target}
    };

    // MPI variables
    int mpi_rank, myrow, mycol;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    gridinfo(mpi_rank, p, q, &myrow, &mycol);

    // Matrix A: figure out local size.
    int64_t mlocA = num_local_rows_cols(m, nb, myrow, p);
    int64_t nlocA = num_local_rows_cols(n, nb, mycol, q);
    int64_t lldA  = blas::max(1, mlocA); // local leading dimension of A
    std::vector<scalar_t> A_data(lldA*nlocA);

    slate::Matrix<scalar_t> A;
    if (origin != slate::Origin::ScaLAPACK) {
        // SLATE allocates CPU or GPU tiles.
        slate::Target origin_target = origin2target(origin);
        A = slate::Matrix<scalar_t>(m, n, nb, p, q, MPI_COMM_WORLD);
        A.insertLocalTiles(origin_target);
    }
    else {
        // Create SLATE matrix from the ScaLAPACK layout.
        A = slate::Matrix<scalar_t>::fromScaLAPACK(
                m, n, &A_data[0], lldA, nb, p, q, MPI_COMM_WORLD);
    }

    // if reference run is required, copy test data
    std::vector<scalar_t> Aref_data;
    slate::Matrix<scalar_t> Aref;
    if (check || ref) {
        // For simplicity, always use ScaLAPACK format for ref matrices.
        Aref_data.resize( lldA*nlocA );
        Aref = slate::Matrix<scalar_t>::fromScaLAPACK(
                   m,  n, &Aref_data[0], lldA, nb, p, q, MPI_COMM_WORLD);
    }

    if (! ref_only) {
        if (trace) slate::trace::Trace::on();
        else slate::trace::Trace::off();

        //==================================================
        // Run SLATE test.
        // Set A by alpha on off-diagonal entries
        //           beta on the diagonal entries.
        //==================================================
        double time = barrier_get_wtime(MPI_COMM_WORLD);

        slate::set(alpha, beta, A, opts);

        time = barrier_get_wtime(MPI_COMM_WORLD) - time;

        if (trace) slate::trace::Trace::finish();

        // compute and save timing/performance
        params.time() = time;

        if (verbose > 1)
            print_matrix( "A", A );

    }

    if (check || ref) {
        #ifdef SLATE_HAVE_SCALAPACK
            real_t norm = slate::norm(slate::Norm::One, A);
            // comparison with reference routine from ScaLAPACK

            // BLACS/MPI variables
            int ictxt, p_, q_, myrow_, mycol_, info;
            int A_desc[9];
            int mpi_rank_ = 0, nprocs = 1;

            // initialize BLACS and ScaLAPACK
            Cblacs_pinfo(&mpi_rank_, &nprocs);
            slate_assert( mpi_rank_ == mpi_rank );
            slate_assert(p*q <= nprocs);
            Cblacs_get(-1, 0, &ictxt);
            Cblacs_gridinit(&ictxt, "Col", p, q);
            Cblacs_gridinfo(ictxt, &p_, &q_, &myrow_, &mycol_);
            slate_assert( p == p_ );
            slate_assert( q == q_ );
            slate_assert( myrow == myrow_ );
            slate_assert( mycol == mycol_ );

            scalapack_descinit(A_desc, m, n, nb, nb, 0, 0, ictxt, lldA, &info);
            slate_assert(info == 0);

            // set MKL num threads appropriately for parallel BLAS
            int omp_num_threads;
            #pragma omp parallel
            { omp_num_threads = omp_get_num_threads(); }
            int saved_num_threads = slate_set_num_blas_threads(omp_num_threads);

            //==================================================
            // Run ScaLAPACK reference routine.
            //==================================================
            double time = barrier_get_wtime(MPI_COMM_WORLD);

            scalapack_plaset(uplo2str(uplo), m, n, alpha, beta, &Aref_data[0], 1, 1, A_desc );
            slate_assert(info == 0);

            time = barrier_get_wtime(MPI_COMM_WORLD) - time;

            if (verbose >= 2)
                print_matrix("Aref", mlocA, nlocA, &Aref_data[0], lldA, p, q, MPI_COMM_WORLD);

            // get differences A = A - Aref
            slate::add(-one, Aref, one, A);

            if (verbose >= 2)
                print_matrix("Diff", A);

            // norm(A - Aref)
            real_t diff_norm = slate::norm(slate::Norm::One, A);

            params.ref_time() = time;

            real_t error = diff_norm / ( n + norm );

            params.error() = error;

            params.okay() = (params.error() == 0);  // Copy should be exact.

            slate_set_num_blas_threads(saved_num_threads);

            Cblacs_gridexit(ictxt);
            //Cblacs_exit(1) does not handle re-entering
        #endif
    }
}

// -----------------------------------------------------------------------------
void test_set(Params& params, bool run)
{
    switch (params.datatype()) {
        case testsweeper::DataType::Integer:
            throw std::exception();
            break;

        case testsweeper::DataType::Single:
            test_set_work<float> (params, run);
            break;

        case testsweeper::DataType::Double:
            test_set_work<double> (params, run);
            break;

        case testsweeper::DataType::SingleComplex:
            test_set_work<std::complex<float>> (params, run);
            break;

        case testsweeper::DataType::DoubleComplex:
            test_set_work<std::complex<double>> (params, run);
            break;
    }
}