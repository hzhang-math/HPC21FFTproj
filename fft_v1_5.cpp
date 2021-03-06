#include <iostream>
#include <stdio.h>
#include <math.h> // to use M_PI
#include <mpi.h>
#include <complex>
#include <fftw3.h>
#include <algorithm> // for min

// mpic++ -std=c++14 -O3 fft_v1_5.cpp -lfftw3
// mpiexec -np 4 ./a.out

using namespace std::complex_literals;

void dft_naive(std::complex<double> *fx, std::complex<double> *fk, int N);
void fft_serial(std::complex<double> *fx, std::complex<double> *fk, int N, std::complex<double> *omega_);
double get_error(std::complex<double> *fk, fftw_complex *out, int N);
int my_log2(int N);
int reverse_bits(int x, int s);
int get_partner(int rank, int size, int j);
void fft_mpi(std::complex<double> *fx, std::complex<double> *fk, int N, std::complex<double> *omega_, int rank, int size); //, double tt1);
int my_pow2(int a);


int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    for (int a=7; a<21; a++) {
		int N = my_pow2(a);
        if (rank == 0) {
	        printf("N = %d \n \n", N);
	    }

	    std::complex<double> *omega_ = (std::complex<double> *) malloc(N* sizeof(std::complex<double>));

        // array of exp(-1i*j*k*2pi/N)
        for (int j=0; j<N; j++) {
            omega_[j] = exp(-1i*(2.*M_PI*j/N));
        }

        std::complex<double> *fx = (std::complex<double> *) malloc(N* sizeof(std::complex<double>));
        std::complex<double> *fx_copy = (std::complex<double> *) malloc(N* sizeof(std::complex<double>));
        std::complex<double> *fx_copy2 = (std::complex<double> *) malloc(N* sizeof(std::complex<double>));
   	    std::complex<double> *fk = (std::complex<double> *) calloc(N, sizeof(std::complex<double>));
        std::complex<double> *fk2 = (std::complex<double> *) calloc(N, sizeof(std::complex<double>));
        std::complex<double> *fk3 = (std::complex<double> *) calloc(N, sizeof(std::complex<double>));

        // initialize vector to take fft of
        for (int j=0; j<N; j++) {
            fx[j] = sin(2*j)+cos(3*j)*1i;
            fx_copy[j] = fx[j];
            fx_copy2[j] = fx[j];
        }

        // do MPI version
        double tt1 = MPI_Wtime();
	    fft_mpi(fx_copy2, fk3, N, omega_, rank, size); //, tt1);
	    tt1 = MPI_Wtime() - tt1;

        // do fftw version, page 3 of http://www.fftw.org/fftw3.pdf
        fftw_complex *in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    	fftw_complex *out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);;
    	fftw_plan p = fftw_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);;

    	for (int j=0; j<N; j++) {
            in[j][0] = sin(2*j);
            in[j][1] = cos(3*j);
    	}

    	if (rank == 0) {
            double tt3 = MPI_Wtime();
	        fft_serial(fx_copy, fk2, N, omega_);
	        tt3 = MPI_Wtime() - tt3;

	        double tt2 = MPI_Wtime();
            fftw_execute(p);
	        tt2 = MPI_Wtime() - tt2;

            printf("time for fftw = %g \n", tt2);

            printf("error for fft_serial = %f \n", get_error(fk2,out,N));
            printf("time for fft_serial = %g \n", tt3);

            printf("error for fft_mpi = %f \n", get_error(fk3,out,N));
            printf("time for fft_mpi = %g \n \n", tt1);

	        fftw_destroy_plan(p);
	        fftw_free(in);
	        fftw_free(out);
	        free(omega_);
	        free(fx);
	        free(fx_copy);
	        free(fx_copy2);
    	    free(fk);
	        free(fk2);
	        free(fk3);
        }

	MPI_Barrier(MPI_COMM_WORLD);
    }

    MPI_Finalize();
    return 0;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

void dft_naive(std::complex<double> *fx, std::complex<double> *fk, int N) {
    for (int j=0; j<N; j++) {  // put these exponential values in an input matrix to avoid calculation
        for (int k=0; k<N; k++) {
            fk[j] += exp(-1i*(2.*M_PI*j*k/N)) * fx[k];
        }
    }
}

//------------------------------------------------------------------------------

double get_error(std::complex<double> *fk, fftw_complex *out, int N) {
    double err{};

    for (int j=0; j<N; j++) {
        err += (real(fk[j])-out[j][0]) * (real(fk[j])-out[j][0]) +
                (imag(fk[j])-out[j][1])* (imag(fk[j])-out[j][1]);
    }

    return sqrt(err);
}

//------------------------------------------------------------------------------

int my_log2(int N) {
    int l{0}, N_temp{N};
    while (N_temp > 1) {
        N_temp /= 2;
        l += 1;
    }
    return l;
}

//------------------------------------------------------------------------------

int my_pow2(int a) {
    int e{2}, a_temp{a};
    while (a_temp > 1) {
	a_temp -= 1;
	e *= 2;
    }
    return e;
}

//-------------------------------------------------------------------------------

int reverse_bits(int x, int s) {
    int x_reverse{}, temp;

    for (int j=0; j<s; j++) {
        temp = (x>>j) & 1;
        x_reverse += temp * (1<<(s-j-1));
    }
    return x_reverse;
}

//------------------------------------------------------------------------------

int get_partner(int rank, int size, int j) {
    int p = my_log2(size);
    if (1 & (rank >> (p-1-j)))
        return (rank - (size >> (j+1)));
    else
        return (rank + (size >> (j+1)));
}

//------------------------------------------------------------------------------

void fft_serial(std::complex<double> *fx, std::complex<double> *fk, int N, std::complex<double> *omega_) {
    int s = my_log2(N), index0, index1;
    int mask; // for inserting 0 or 1 in the bitstring
    std::complex<double> t0, t1, w, x;

    for (int j=0; j<s; j++) {
        // https://en.wikipedia.org/wiki/Bitwise_operations_in_C
        mask = (1 << (s-1-j)) - 1;
        for (int k=0; k<N/2; k++) {
            index0 = ((k & ~mask) << 1) | (k & mask);
            index1 = ((k & ~mask) << 1) | (k & mask) + (1 << (s-1-j));

            t0 = fx[index0];
            t1 = fx[index1];

            w = omega_[reverse_bits(index0 >> s-1-j, s)];
            x = w*t1;

            fx[index0] = t0 + x;
            fx[index1] = t0 - x;
        }
    }

    // rearrange output to give correct answer
    for (int k=0; k<N; k++) {
        fk[k] = fx[reverse_bits(k,s)];
    }
}

//------------------------------------------------------------------------------

void fft_mpi(std::complex<double> *fx, std::complex<double> *fk, int N, std::complex<double> *omega_, int rank, int size) { //, double tt1) {
    MPI_Status status;
    int s = my_log2(N), p = my_log2(size);
    int mask, index0, index1, partner, k_global, Nlocal = N/size;
    std::complex<double> t0, t1, w, x;

    std::complex<double> *fxlocal = (std::complex<double> *) malloc(Nlocal* sizeof(std::complex<double>));
    std::complex<double> *fxin = (std::complex<double> *) malloc(Nlocal* sizeof(std::complex<double>));

    for (int j=0; j<Nlocal; j++) {
        fxlocal[j] = fx[rank*Nlocal + j];
    }

    // part with communication
    for (int j=0; j<p; j++) {
        // processor to communicate with
        partner = get_partner(rank, size, j);

        MPI_Sendrecv(fxlocal, Nlocal, MPI_DOUBLE_COMPLEX, partner, 310,
               fxin, Nlocal, MPI_DOUBLE_COMPLEX, partner, 310, MPI_COMM_WORLD, &status);

        for (int k=0; k<Nlocal; k++) {
            index0 = k + std::min(rank,partner)*Nlocal;
            w = omega_[reverse_bits(index0 >> s-1-j, s)];

            if (partner>rank) {
                t0 = fxlocal[k];
                t1 = fxin[k];
                x = w*t1;
                fxlocal[k] = t0 + x;
            }
            else {
                t0 = fxin[k];
                t1 = fxlocal[k];
                x = w*t1;
                fxlocal[k] = t0 - x;
            }
        }
    }

    // part without communication
    for (int j=p; j<s; j++) {
        mask = (1 << (s-1-j)) - 1;
        for (int k=0; k<Nlocal/2; k++) {
            k_global = k + rank*Nlocal/2;

            index0 = ((k_global & ~mask) << 1) | (k_global & mask);
            index1 = ((k_global & ~mask) << 1) | (k_global & mask) + (1 << (s-1-j));

            t0 = fxlocal[index0 % Nlocal];
            t1 = fxlocal[index1 % Nlocal];

            w = omega_[reverse_bits(index0 >> s-1-j, s)];
            x = w*t1;

            fxlocal[index0 % Nlocal] = t0 + x;
            fxlocal[index1 % Nlocal] = t0 - x;
        }
    }

    MPI_Gather(fxlocal, Nlocal, MPI_DOUBLE_COMPLEX, fx, Nlocal,
               MPI_DOUBLE_COMPLEX, 0, MPI_COMM_WORLD);
    if (rank == 0) {
        for (int k=0; k<N; k++) {
            fk[k] = fx[reverse_bits(k,s)];
        }
    }
//    tt1 = MPI_Wtime() - tt1;
}
