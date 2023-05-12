// Copyright (c) 2017-2022, University of Tennessee. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// This program is free software: you can redistribute it and/or modify it under
// the terms of the BSD 3-Clause license. See the accompanying LICENSE file.

#include "slate/slate.hh"
#include "internal/internal.hh"
#include "lapack/flops.hh"
#include "test.hh"
#include "print_matrix.hh"
#include "grid_utils.hh"
#include "matrix_utils.hh"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>


//------------------------------------------------------------------------------
template <typename scalar_t>
void test_unmbr_tb2bd_work(Params& params, bool run)
{
    using real_t = blas::real_type<scalar_t>;
    using blas::real;
    using blas::imag;

    // Constants
    const scalar_t zero = 0;
    const scalar_t one  = 1;
    const auto mpi_real_type = slate::mpi_type< blas::real_type<scalar_t> >::value;

    // get & mark input values
    int64_t m = params.dim.m();
    int64_t n = params.dim.n();
    int64_t nb = params.nb();
    int64_t ib = params.ib();
    int64_t band = nb;  // for now use band == nb.
    int64_t p = params.grid.m();
    int64_t q = params.grid.n();
    slate::Uplo uplo = slate::Uplo::Upper;
    bool upper = uplo == slate::Uplo::Upper;
    bool check = params.check() == 'y';
    bool trace = params.trace() == 'y';
    slate::Origin origin = params.origin();
    slate::Target target = params.target();
    params.matrix.mark();

    // mark non-standard output values
    params.time();
    params.gflops();
    params.ref_time();
    params.ref_gflops();
    params.error2();
    params.ortho_U();
    params.ortho_V();
    params.ortho_U.name( "U orth." );
    params.ortho_V.name( "VT orth." );
    params.error.name( "value err" );
    params.error2.name( "back err" );

    if (! run)
        return;

    if (origin == slate::Origin::ScaLAPACK) {
        params.msg() = "skipping: currently origin=scalapack is not supported";
        return;
    }
    if (origin == slate::Origin::Devices) {
        params.msg() = "skipping: currently origin=devices is not supported";
        return;
    }

    slate::Options const opts =  {
        {slate::Option::Target, target},
        {slate::Option::InnerBlocking, ib}
    };

    // MPI variables
    int mpi_rank, myrow, mycol;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    gridinfo(mpi_rank, p, q, &myrow, &mycol);

    int64_t lda = m;
    int64_t seed[] = {0, 1, 2, 3};

    slate::Target origin_target = origin2target(origin);
    //slate::Matrix<scalar_t> Afull(m, n, nb, p, q, MPI_COMM_WORLD);
    //Afull.insertLocalTiles(origin_target);
    //slate::generate_matrix( params.matrix, Afull);
    std::vector<scalar_t> Afull_data( lda*n );
    auto Afull = slate::Matrix<scalar_t>::fromLAPACK(
            m, n, &Afull_data[0], lda, nb, p, q, MPI_COMM_WORLD);
    lapack::larnv(1, seed, Afull_data.size(), &Afull_data[0]);

    //slate::Matrix<scalar_t> Acopy(m, n, nb, p, q, MPI_COMM_WORLD);
    //Acopy.insertLocalTiles(origin_target);
    std::vector<scalar_t> Acopy_data( lda*n );
    auto Acopy = slate::Matrix<scalar_t>::fromLAPACK(
            m, n, &Acopy_data[0], lda, nb, p, q, MPI_COMM_WORLD);

    print_matrix( "Afull", Afull, params );

    // 1. Reduce to band form.
    #if 1
    // Test full matrix.
    printf("\n Test full matrix.\n");
    copy(Afull, Acopy);
    slate::TriangularFactors<scalar_t> TU, TV;
    ge2tb(Afull, TU, TV, opts);

    #if 0
    printf("\n Test band matrix reduced using ge2tb to reduce it. \n");
    // Test band matrix reduced using ge2tb to reduce it.
    // Zero outside the band.
    slate::copy(Afull, Acopy, opts);
    for (int64_t j = 0; j < n; ++j) {
        if (upper) {
            for (int64_t i = 0; i < m; ++i) {
                if (j > i+band || j < i)
                    Acopy_data[i + j*lda] = 0;
            }
        }
        else { // lower
            for (int64_t i = 0; i < n; ++i) {
                if (j < i-band || j > i)
                    Acopy_data[i + j*lda] = 0;
            }
        }
    }
    print_matrix( "Aband", Acopy, params );
    #endif
    #endif

    // Copy band of Afull, currently to rank 0.
    auto Aband = slate::TriangularBandMatrix<scalar_t>(
        slate::Uplo::Upper, slate::Diag::NonUnit, n, band, nb,
        1, 1, MPI_COMM_WORLD);
    Aband.insertLocalTiles(origin_target);
    auto Ahat = Afull.slice( 0, Afull.n()-1, 0, Afull.n()-1 );
    Aband.ge2tbGather( Ahat );

    // Set V = Identity.
    slate::Matrix<scalar_t> VT(n, n, nb, p, q, MPI_COMM_WORLD);
    VT.insertLocalTiles(origin_target);
    set(zero, one, VT);

    // 1-d V matrix
    slate::Matrix<scalar_t> V1d(VT.m(), VT.n(), VT.tileNb(0), 1, p*q, MPI_COMM_WORLD);
    V1d.insertLocalTiles(origin_target);

    // Set U = Identity.
    slate::Matrix<scalar_t> U(m, n, nb, p, q, MPI_COMM_WORLD);
    U.insertLocalTiles(origin_target);
    set(zero, one, U);
    print_matrix( "U_iden", U, params );
    auto Uhat = U.slice(0, U.n()-1, 0, U.n()-1);
    print_matrix( "Uhat", Uhat, params );

    // 1-d U matrix
    slate::Matrix<scalar_t> U1d(Uhat.m(), Uhat.n(), Uhat.tileNb(0), 1, p*q, MPI_COMM_WORLD);
    U1d.insertLocalTiles(origin_target);

    //--------------------
    // [code copied from heev.cc]
    // Matrix to store Householder vectors.
    // Could pack into a lower triangular matrix, but we store each
    // parallelogram in a 2nb-by-nb tile, with nt(nt + 1)/2 tiles.
    int64_t mt = Uhat.mt();
    int64_t nt = Uhat.nt();

    int64_t vm = 2*nb;
    int64_t vn = nt*(nt + 1)/2*nb;

    int64_t un = mt*(mt + 1)/2*nb;
    int64_t um = 2*nb;

    int64_t min_mn = std::min(m, n);
    std::vector<real_t> Sigma(min_mn);
    std::vector<real_t> E(min_mn - 1);  // super-diagonal

    slate::Matrix<scalar_t> V2(vm, vn, vm, nb, 1, 1, MPI_COMM_WORLD);
    slate::Matrix<scalar_t> U2(um, un, um, nb, 1, 1, MPI_COMM_WORLD);

    // Compute tridiagonal and Householder vectors V.
    if (mpi_rank == 0) {
        V2.insertLocalTiles(origin_target);
        U2.insertLocalTiles(origin_target);
        slate::tb2bd(Aband, U2, V2);
        // Copy diagonal & super-diagonal.
        slate::internal::copytb2bd(Aband, Sigma, E);
    }

    if (trace)
        slate::trace::Trace::on();
    else
        slate::trace::Trace::off();

    MPI_Bcast( &Sigma[0], min_mn, mpi_real_type, 0, MPI_COMM_WORLD );
    MPI_Bcast( &E[0], min_mn-1, mpi_real_type, 0, MPI_COMM_WORLD );

    std::vector<real_t> Sigma_ref = Sigma;
    std::vector<real_t> Eref = E;

    print_matrix("D", 1, min_mn,   &Sigma[0], 1, params);
    print_matrix("E", 1, min_mn-1, &E[0],  1, params);

    // Call bdsqr to compute the singular values Sigma
    slate::bdsqr<scalar_t>(slate::Job::Vec, slate::Job::Vec, Sigma, E, Uhat, VT, opts);

    print_matrix("Sigma", 1, min_mn, &Sigma[0],  1, params);
    print_matrix("U_bdsqr", U, params);
    print_matrix("VT_bdsqr", VT, params);

    double time = barrier_get_wtime(MPI_COMM_WORLD);
    // V V1
    // V1T VT
    //auto V = conj_transpose(VT);
    //

    //==================================================
    // Run SLATE test.
    //==================================================
    U1d.redistribute(Uhat);
    slate::unmtr_hb2st(slate::Side::Left, slate::Op::NoTrans, U2, U1d, opts);

    print_matrix( "before_redistribute_U1d", U1d, params );

    Uhat.redistribute(U1d);

    print_matrix( "After_redistribute_Uhat", Uhat, params );

    print_matrix( "U_before_ge2tb", U, params );
    print_matrix("TUlocal",  TU[0], params);
    print_matrix("TUreduce", TU[1], params);
    slate::unmbr_ge2tb( slate::Side::Left, slate::Op::NoTrans, Afull, TU, U, opts );
    print_matrix( "U", U, params );

    //auto V = conj_transpose(VT);
    //auto R = V.emptyLike();
    //R.insertLocalTiles();
    //slate::copy(V, R, opts);
    //print_matrix("V", V, params);
    int64_t nb_A = VT.tileNb( 0 );
    slate::GridOrder grid_order;
    int nprow, npcol;
    VT.gridinfo( &grid_order, &nprow, &npcol, &myrow, &mycol );
    std::function<int64_t (int64_t j)>
        tileNb = [n, nb_A] (int64_t j) {
            return (j + 1)*nb_A > n ? n%nb_A : nb_A;
        };

    std::function<int (std::tuple<int64_t, int64_t> ij)>
        tileRank = [nprow, npcol]( std::tuple<int64_t, int64_t> ij ) {
            int64_t i = std::get<0>( ij );
            int64_t j = std::get<1>( ij );
            return int( (i%nprow)*npcol + j%npcol );
        };

    int num_devices = blas::get_device_count();
    std::function<int (std::tuple<int64_t, int64_t> ij)>
        tileDevice = [nprow, num_devices]( std::tuple<int64_t, int64_t> ij ) {
            int64_t i = std::get<0>( ij );
            return int( i/nprow )%num_devices;
        };


    slate::Matrix<scalar_t> V(
           n, n, tileNb, tileNb, tileRank, tileDevice, MPI_COMM_WORLD );
    V.insertLocalTiles(origin_target);
    auto R = conj_transpose(VT);

    copy(R, V);
    V1d.redistribute(V);

    slate::unmbr_tb2bd(slate::Side::Left, slate::Op::NoTrans, V2, V1d, opts);
    V.redistribute(V1d);

    auto RT = conj_transpose(V);
    slate::copy(RT, VT, opts);
    print_matrix( "VT", VT, params );
    print_matrix( "RT", RT, params );
    print_matrix( "V", V, params );

    slate::unmbr_ge2tb( slate::Side::Right, slate::Op::NoTrans, Afull, TV, VT, opts );

    time = barrier_get_wtime(MPI_COMM_WORLD) - time;

    if (trace)
        slate::trace::Trace::finish();

    // compute and save timing/performance
    params.time() = time;
    // todo: using unmqr's flop count, which is an estimation for unmbr_tb2bd
    double gflop = lapack::Gflop<scalar_t>::unmqr(lapack::Side::Left, n, n, n);
    params.gflops() = gflop / time;

    if (check) {
        real_t tol = params.tol() * std::numeric_limits<real_t>::epsilon() / 2;

        //==================================================
        // Test results
        // Relative forward error: || D - Dref || / || Dref ||.
        //==================================================
        scalar_t dummy[1];  // U, VT, C not needed for NoVec
        lapack::bdsqr(uplo, n, 0, 0, 0,
                      &Sigma_ref[0], &Eref[0], dummy, 1, dummy, 1, dummy, 1);
        real_t Sigma_norm = blas::nrm2(Sigma_ref.size(), &Sigma_ref[0], 1);
        blas::axpy(Sigma_ref.size(), -1.0, &Sigma[0], 1, &Sigma_ref[0], 1);
        params.error() = blas::nrm2(Sigma_ref.size(), &Sigma_ref[0], 1)
                       / Sigma_norm;
        params.okay() = params.error() <= tol;

        //==================================================
        // Test results
        // || I - V^H V || / n < tol
        //==================================================
        slate::Matrix<scalar_t> Iden( n, n, nb, p, q, MPI_COMM_WORLD );
        Iden.insertLocalTiles(origin_target);
        set(zero, one, Iden);

        auto VT2 = conj_transpose(VT);

        //slate::gemm(-one, VT, VT2, one, Iden);
        slate::gemm(-one, VT2, VT, one, Iden);
        print_matrix( "I_VH*V", Iden, params );

        params.ortho_V() = slate::norm(slate::Norm::One, Iden) / n;

        // If slate::unmbr_tb2bd() fails to update Q, then Q=I.
        // Q is still orthogonal but this should be a failure.
        // todo remove this if block when the backward error
        // check is implemented.
        if (slate::norm(slate::Norm::One, Iden) == zero) {
            params.okay() = false;
            printf("\n Why zero ?\n");
            return;
        }
        params.okay() = params.okay() && (params.ortho_V() <= tol);

        //==================================================
        // Test results
        // || I - U^H U || / n < tol
        //==================================================
        set(zero, one, Iden);
        auto UH = conj_transpose( U );
        slate::gemm( -one, UH, U, one, Iden );
        params.ortho_U() = slate::norm( slate::Norm::One, Iden ) / n;
        params.okay() = params.okay() && (params.ortho_U() <= tol);

        //==================================================
        // Test results
        // || A - U S V^H || / (n || A ||) < tol
        //==================================================
        // Compute norm Afull to scale error
        real_t Anorm = slate::norm( slate::Norm::One, Afull );

        // Scale V by Sigma to get Sigma V
        //copy(VT2, R);
        slate::scale_row_col( slate::Equed::Col, Sigma, Sigma, U );

        slate::gemm( -one, U, VT, one, Acopy );

        print_matrix( "Afull-U*S*VT", Acopy, params );

        params.error2() = slate::norm( slate::Norm::One, Acopy ) / (Anorm * n);

        params.okay() = params.okay() && (params.error2() <= tol);
    }
}

// -----------------------------------------------------------------------------
void test_unmbr_tb2bd(Params& params, bool run)
{
    switch (params.datatype()) {
        case testsweeper::DataType::Integer:
            throw std::exception();
            break;

        case testsweeper::DataType::Single:
            test_unmbr_tb2bd_work<float> (params, run);
            break;

        case testsweeper::DataType::Double:
            test_unmbr_tb2bd_work<double> (params, run);
            break;

        case testsweeper::DataType::SingleComplex:
            test_unmbr_tb2bd_work< std::complex<float> > (params, run);
            break;

        case testsweeper::DataType::DoubleComplex:
            test_unmbr_tb2bd_work< std::complex<double> > (params, run);
            break;
    }
}
