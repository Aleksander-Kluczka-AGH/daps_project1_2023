#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#include <mpi.h>

#define ND [[nodiscard]]

namespace global
{
MPI_Status mpi_status;
int process_count{};
int rank{};
MPI_Datatype MPI_COMPLEX_T{};

int input_size{};
}  // namespace global

namespace logger
{
void all(const char*, auto...);
void master(const char*, auto...);
void slave(const char*, auto...);
}  // namespace logger

struct Complex
{
    float real{};
    float img{};
};

void initGlobals();
ND int reverseBits(int, int);
ND int getProcessCount();
ND int getProcessRank();
ND MPI_Datatype registerMpiDatatype(int,
    const std::vector<int>&,
    const std::vector<MPI_Aint>&,
    const std::vector<MPI_Datatype>&);
ND std::vector<Complex> getInputValues(const char*);
void showResults(const Complex*, double, double);

int main(int argc, char** argv)
{
    MPI_Init(&argc, &argv);
    initGlobals();

    std::vector<Complex> input_values = getInputValues("../res/input.txt");


    Complex seq[global::input_size]{};
    Complex temp[global::input_size]{};

    if(global::rank == 0)
    {
        const int max_bit_width = std::log2f(global::input_size);
        for(int i = 1; i < global::input_size; i++)
        {
            seq[i].real = input_values[reverseBits(i - 1, max_bit_width) + 1].real;
            seq[i].img = 0.0;
        }
    }

    logger::master("broadcast initial sequence\n");
    MPI_Bcast(seq, global::input_size, global::MPI_COMPLEX_T, 0, MPI_COMM_WORLD);


    const double starttime = MPI_Wtime();
    for(int div = 1, key = std::log2f(global::input_size - 1); key > 0; key--, div *= 2)
    {
        if(global::rank != 0)
        {
            using global::rank;

            logger::slave("beginning compute...\n");
            const auto is_even = ((rank + div - 1) / div) % 2;
            const auto is_odd = 1 - is_even;
            const auto butterfly_index = M_PI * ((rank - 1) % (div * 2)) / div;

            temp[rank].real = seq[rank - (div * is_odd)].real
                              + (std::cos(butterfly_index) * (seq[rank + (div * is_even)].real))
                              + (std::sin(butterfly_index) * (seq[rank + (div * is_even)].img));

            temp[rank].img = seq[rank - (div * is_odd)].img
                             + (std::cos(butterfly_index) * (seq[rank + (div * is_even)].img))
                             - (std::sin(butterfly_index) * (seq[rank + (div * is_even)].real));

            MPI_Send(&temp[rank], 1, global::MPI_COMPLEX_T, 0, 0, MPI_COMM_WORLD);
            logger::slave("ending compute...\n");
        }
        else
        {
            logger::master("beginning receiving temps... (count = %d)\n", global::input_size);
            for(int i = 1; i < global::input_size; i++)
            {
                MPI_Recv(&temp[i],
                    1,
                    global::MPI_COMPLEX_T,
                    i,
                    0,
                    MPI_COMM_WORLD,
                    &global::mpi_status);
                logger::master(" -- received iteration (i = %d, status = %d, temp[i] = {%f, %f})\n",
                    i,
                    global::mpi_status,
                    temp[i].real,
                    temp[i].img);
            }
            logger::master("finished receiving temps\n");
        }

        MPI_Barrier(MPI_COMM_WORLD);

        if(global::rank == 0)
        {
            for(int i = 1; i < global::input_size; i++)
            {
                seq[i].real = temp[i].real;
                seq[i].img = temp[i].img;
            }
        }

        logger::master("broadcast final sequence\n");
        MPI_Bcast(seq, global::input_size, global::MPI_COMPLEX_T, 0, MPI_COMM_WORLD);
    }

    const double endtime = MPI_Wtime();
    showResults(seq, starttime, endtime);

    MPI_Finalize();
    return 0;
}

////////////////

void initGlobals()
{
    global::process_count = getProcessCount();
    global::rank = getProcessRank();

    global::MPI_COMPLEX_T = registerMpiDatatype(sizeof(Complex) / sizeof(float),
        {sizeof(float), sizeof(float)},
        {0, 4},
        {MPI_FLOAT, MPI_FLOAT});
}

int reverseBits(int number, int bit_range)
{
    int reverse_number = 0;
    for(int i = 0; i < bit_range; i++)
    {
        reverse_number |= ((number >> i) & 1) << (bit_range - 1 - i);
    }
    return reverse_number;
}

int getProcessCount()
{
    int process_count{};
    MPI_Comm_size(MPI_COMM_WORLD, &process_count);
    return process_count;
}

int getProcessRank()
{
    int rank = -1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    return rank;
}

MPI_Datatype registerMpiDatatype(int num_of_values,
    const std::vector<int>& sizes_of_values,
    const std::vector<MPI_Aint>& strides_of_values,
    const std::vector<MPI_Datatype>& types_of_values)
{
    MPI_Datatype new_type;
    MPI_Type_create_struct(num_of_values,
        sizes_of_values.data(),
        strides_of_values.data(),
        types_of_values.data(),
        &new_type);
    MPI_Type_commit(&new_type);
    return new_type;
}

std::vector<Complex> getInputValues(const char* path)
{
    std::vector<Complex> result;
    if(global::rank == 0)
    {
        std::ifstream file(path);

        if(not file.is_open())
        {
            return {};
        }

        result.emplace_back(0, 0);

        float real{};
        float img{};
        while(file >> real >> img)
        {
            result.emplace_back(real, img);
        }
        global::input_size = result.size();
    }

    MPI_Bcast(&global::input_size, 1, MPI_INT, 0, MPI_COMM_WORLD);
    return std::move(result);
}

void showResults(const Complex* seq, double starttime, double endtime)
{
    if(0 == global::rank)
    {
        std::printf("\n");

        for(int i = 1; i < global::input_size; i++)
        {
            std::printf("X[%d] : %f", i - 1, seq[i].real);

            if(seq[i].img >= 0)
            {
                std::printf("+j%f\n", seq[i].img);
            }
            else
            {
                std::printf("-j%f\n", seq[i].img - 2 * seq[i].img);
            }
        }

        std::printf("\n");
        std::printf("Total Time : %lf ms\n", (endtime - starttime) * 1000);
        std::printf("\n");
    }
}

namespace logger
{
void master(const char* format, auto... args)
{
    if(global::rank == 0)
    {
        logger::all(format, args...);
    }
}

void slave(const char* format, auto... args)
{
    if(global::rank != 0)
    {
        ::logger::all(format, args...);
    }
}

void all(const char* format, auto... args)
{
#ifdef ENABLE_LOGGING
    const auto rank_idx = not not global::rank;

    std::array<std::string, 2> thread_name = {"master",
        "slave(" + std::to_string(global::rank) + ")"};

    std::stringstream strstr;
    strstr << "LOG | " << thread_name[rank_idx] << " | " << format;
    std::printf(strstr.str().c_str(), args...);
#endif
}
}  // namespace logger
